//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/task.hpp
//
//
//===----------------------------------------------------------------------===//


namespace duckdb {

class Task {
public:
	virtual ~Task() {
	}

	//! Execute the task
	virtual void Execute() = 0;
};

}