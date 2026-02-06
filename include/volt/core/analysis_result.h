#pragma once

#include <nlohmann/json.hpp>
#include <chrono>
#include <string>

namespace Volt{

/**
 * Helper for building consistent JSON results from analysis services.
 * Every service returns json with at least "is_failed" and optionally
 * "error", timing information, and analysis-specific fields.
 */
class AnalysisResult{
public:
	using json = nlohmann::json;

	static json failure(const std::string& error){
		json result;
		result["is_failed"] = true;
		result["error"] = error;
		return result;
	}

	static json success(){
		json result;
		result["is_failed"] = false;
		return result;
	}

	static void addTiming(json& result, const std::chrono::high_resolution_clock::time_point& start){
		auto end = std::chrono::high_resolution_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		result["duration_ms"] = ms;
	}
};

}
