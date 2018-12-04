//===----------------------------------------------------------------------===//
//
//                         DuckDB
//
// execution/operator/helper/physical_limit.hpp
//
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "execution/physical_operator.hpp"

#include "storage/data_table.hpp"

namespace duckdb {

//! PhyisicalLimit represents the LIMIT operator
class PhysicalLimit : public PhysicalOperator {
  public:
	PhysicalLimit(size_t limit, size_t offset)
	    : PhysicalOperator(PhysicalOperatorType::LIMIT), limit(limit),
	      offset(offset) {
	}

	size_t limit;
	size_t offset;

	std::vector<TypeId> GetTypes() override;

	void _GetChunk(ClientContext &context, DataChunk &chunk,
	               PhysicalOperatorState *state) override;

	std::unique_ptr<PhysicalOperatorState>
	GetOperatorState(ExpressionExecutor *parent_executor) override;
};

class PhysicalLimitOperatorState : public PhysicalOperatorState {
  public:
	PhysicalLimitOperatorState(PhysicalOperator *child,
	                           size_t current_offset = 0,
	                           ExpressionExecutor *parent_executor = nullptr)
	    : PhysicalOperatorState(child, parent_executor),
	      current_offset(current_offset) {
	}

	size_t current_offset;
};
} // namespace duckdb