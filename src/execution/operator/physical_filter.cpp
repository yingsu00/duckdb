
#include "execution/operator/physical_filter.hpp"
#include "execution/expression_executor.hpp"

using namespace duckdb;
using namespace std;

vector<TypeId> PhysicalFilter::GetTypes() { return children[0]->GetTypes(); }

void PhysicalFilter::GetChunk(DataChunk &chunk, PhysicalOperatorState *state_) {
	auto state = reinterpret_cast<PhysicalOperatorState *>(state_);
	chunk.Reset();

	do {
		children[0]->GetChunk(state->child_chunk, state->child_state.get());
		if (state->child_chunk.count == 0) {
			return;
		}

		if (expressions.size() == 0) {
			throw Exception(
			    "Attempting to execute a filter without expressions");
		}

		Vector result(TypeId::BOOLEAN, state->child_chunk.count);
		ExpressionExecutor executor(state);
		executor.Execute(expressions[0].get(), result);
		// AND together the remaining filters!
		for (size_t i = 1; i < expressions.size(); i++) {
			auto &expr = expressions[i];
			executor.Merge(expr.get(), result);
		}
		// now generate the selection vector
		bool *matches = (bool *)result.data;
		auto sel_vector = unique_ptr<sel_t[]>(new sel_t[result.count]);
		size_t match_count = 0;
		for (size_t i = 0; i < result.count; i++) {
			if (matches[i]) {
				sel_vector[match_count++] = i;
			}
		}
		if (match_count == result.count) {
			// everything matches! don't need a selection vector!
			sel_vector.reset();
		}
		for (size_t i = 0; i < chunk.column_count; i++) {
			// create a reference to the vector of the child chunk
			chunk.data[i].Reference(state->child_chunk.data[i]);
		}
		// assign the selection vector
		chunk.SetSelVector(move(sel_vector), match_count);
	} while (chunk.count == 0);
}

unique_ptr<PhysicalOperatorState>
PhysicalFilter::GetOperatorState(ExpressionExecutor *parent) {
	return make_unique<PhysicalOperatorState>(children[0].get(), parent);
}
