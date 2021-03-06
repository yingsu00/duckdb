#include "duckdb/execution/execution_context.hpp"
#include "duckdb/execution/physical_operator.hpp"
#include "duckdb/execution/operator/join/physical_delim_join.hpp"
#include "duckdb/execution/operator/scan/physical_chunk_scan.hpp"
#include "duckdb/execution/operator/helper/physical_execute.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

ExecutionContext::ExecutionContext(ClientContext &context) : context(context) {
}

ExecutionContext::~ExecutionContext() {
}

void ExecutionContext::Initialize(unique_ptr<PhysicalOperator> plan) {
	pipelines.clear();
	while (!scheduled_pipelines.empty()) {
		scheduled_pipelines.pop();
	}

	physical_plan = move(plan);
	physical_state = physical_plan->GetOperatorState();

	context.profiler.Initialize(physical_plan.get());

	BuildPipelines(physical_plan.get(), nullptr);
	// schedule pipelines that do not have dependents
	for (auto &pipeline : pipelines) {
		if (pipeline->dependencies.size() == 0) {
			Schedule(pipeline.get());
		}
	}
}

void ExecutionContext::Reset() {
	physical_plan = nullptr;
	physical_state = nullptr;
}

void ExecutionContext::BuildPipelines(PhysicalOperator *op, Pipeline *parent) {
	if (op->IsSink()) {
		// operator is a sink, build a pipeline
		auto pipeline = make_unique<Pipeline>(*this);
		pipeline->sink = (PhysicalSink *)op;
		pipeline->sink_state = pipeline->sink->GetGlobalState(context);
		if (parent) {
			// the parent is dependent on this pipeline to complete
			parent->AddDependency(pipeline.get());
		}
		switch (op->type) {
		case PhysicalOperatorType::INSERT:
		case PhysicalOperatorType::DELETE:
		case PhysicalOperatorType::UPDATE:
		case PhysicalOperatorType::CREATE:
		case PhysicalOperatorType::HASH_GROUP_BY:
		case PhysicalOperatorType::DISTINCT:
		case PhysicalOperatorType::SIMPLE_AGGREGATE:
		case PhysicalOperatorType::WINDOW:
		case PhysicalOperatorType::ORDER_BY:
		case PhysicalOperatorType::TOP_N:
			// single operator, set as child
			pipeline->child = op->children[0].get();
			break;
		case PhysicalOperatorType::NESTED_LOOP_JOIN:
		case PhysicalOperatorType::BLOCKWISE_NL_JOIN:
		case PhysicalOperatorType::HASH_JOIN:
		case PhysicalOperatorType::PIECEWISE_MERGE_JOIN:
			// regular join, create a pipeline with RHS source that sinks into this pipeline
			pipeline->child = op->children[1].get();
			// on the LHS (probe child), we recurse with the current set of pipelines
			BuildPipelines(op->children[0].get(), parent);
			break;
		case PhysicalOperatorType::DELIM_JOIN: {
			// duplicate eliminated join
			auto &delim_join = (PhysicalDelimJoin &)*op;
			// create a pipeline with the duplicate eliminated path as source
			pipeline->child = op->children[0].get();
			// any scan of the duplicate eliminated data on the RHS depends on this pipeline
			// we add an entry to the mapping of (ChunkCollection*) -> (Pipeline*)
			delim_join_dependencies[&delim_join.delim_data] = pipeline.get();
			// recurse into the actual join; any pipelines in there depend on the main pipeline
			BuildPipelines(delim_join.join.get(), parent);
			break;
		}
		default:
			throw NotImplementedException("Unimplemented sink type!");
		}
		// recurse into the pipeline child
		BuildPipelines(pipeline->child, pipeline.get());
		pipelines.push_back(move(pipeline));
	} else {
		// operator is not a sink! recurse in children
		// first check if there is any additional action we need to do depending on the type
		switch (op->type) {
		case PhysicalOperatorType::DELIM_SCAN: {
			auto &chunk_scan = (PhysicalChunkScan &)*op;
			// check if this chunk scan scans a duplicate eliminated join collection
			auto entry = delim_join_dependencies.find(chunk_scan.collection);
			assert(entry != delim_join_dependencies.end());
			// this chunk scan introduces a dependency to the current pipeline
			// namely a dependency on the duplicate elimination pipeline to finish
			assert(parent);
			parent->AddDependency(entry->second);
			break;
		}
		case PhysicalOperatorType::EXECUTE: {
			// EXECUTE statement: build pipeline on child
			auto &execute = (PhysicalExecute &)*op;
			BuildPipelines(execute.plan, parent);
			break;
		}
		default:
			break;
		}
		for (auto &child : op->children) {
			BuildPipelines(child.get(), parent);
		}
	}
};

void ExecutionContext::Schedule(Pipeline *pipeline) {
	assert(pipeline->dependencies.size() == 0);
	scheduled_pipelines.push(pipeline);
}

vector<TypeId> ExecutionContext::GetTypes() {
	assert(physical_plan);
	return physical_plan->GetTypes();
}

unique_ptr<DataChunk> ExecutionContext::FetchChunk() {
	if (pipelines.size() > 0) {
		// execute scheduled pipelines until we are done
		while (!scheduled_pipelines.empty()) {
			auto pipeline = scheduled_pipelines.front();
			scheduled_pipelines.pop();

			pipeline->Execute(context);
		}
		assert(pipelines.size() == 0);
	}
	assert(physical_plan);

	auto chunk = make_unique<DataChunk>();
	// run the plan to get the next chunks
	physical_plan->InitializeChunk(*chunk);
	physical_plan->GetChunk(context, *chunk, physical_state.get());
	return chunk;
}

void ExecutionContext::ErasePipeline(Pipeline *pipeline) {
	pipelines.erase(std::find_if(pipelines.begin(), pipelines.end(),
	                             [&](std::unique_ptr<Pipeline> &p) { return p.get() == pipeline; }));
}

} // namespace duckdb
