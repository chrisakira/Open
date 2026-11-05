#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "test_support.hpp"

// The `open_sim` contract, automated at last: AGENTS.md says `cpp/` must not include Godot
// headers or link `godot-cpp`, and until now nothing but convention enforced it. Break these
// catches: a `godot::` type that crept into a simulation header because it was convenient, a
// `#include <godot_cpp/...>` in a `.cpp` that "only needs one variant", an `Extension`-flavoured
// token in a public signature, and a `target_link_libraries(open_sim ... godot-cpp)` line that
// would silently weld the simulation to the engine.
//
// It reads the tree as text, so it names `file:line` and stops at what a text scan can see:
// linkage arriving through a variable, a generator expression or a parent scope's properties is
// outside its reach, and an include that only resolves inside `extension/` is the compiler's
// business. The engine-facing code has exactly one home, and this test keeps it there.

namespace {

constexpr const char *kSourceRoot = OPEN_SIM_SOURCE_DIR;

std::string to_lower(const std::string &text) {
	std::string made = text;
	std::transform(made.begin(), made.end(), made.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return made;
}

bool is_include_directive(const std::string &line) {
	const std::size_t start = line.find_first_not_of(" \t");
	return start != std::string::npos && line.compare(start, 8, "#include") == 0;
}

// The engine's own spellings, all lowercase: a namespace, a header path, a package name, the
// extension API. Prose that merely names the engine ("The Godot bridge checks ids first") is
// capitalised, and that is what lets the simulation keep explaining itself in comments.
bool names_the_engine(const std::string &lower) {
	static const char *const kTokens[] = {"godot::", "godot_", "godot/", "godot-cpp",
										  "gdextension", "godotengine"};
	for (const char *token : kTokens) {
		if (lower.find(token) != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool is_source_file(const std::filesystem::path &file) {
	static const std::vector<std::string> kExtensions = {".hpp", ".h", ".cpp", ".ipp", ".inl"};
	const std::string extension = file.extension().string();
	return std::find(kExtensions.begin(), kExtensions.end(), extension) != kExtensions.end();
}

// What one scan produced: the forbidden lines as `path:line: text`, and the names of the files
// it really opened - so a wrong root path fails as "it read nothing", not as "nothing was
// forbidden".
struct Scan {
	std::vector<std::string> offenders;
	std::vector<std::string> read;
};

Scan scan_directory(const std::filesystem::path &directory) {
	Scan made;
	if (!std::filesystem::is_directory(directory)) {
		made.offenders.push_back(directory.string() + ": the directory is not there to be scanned");
		return made;
	}
	std::vector<std::filesystem::path> paths;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
		if (entry.is_regular_file() && is_source_file(entry.path())) {
			paths.push_back(entry.path());
		}
	}
	std::sort(paths.begin(), paths.end());
	for (const auto &path : paths) {
		std::ifstream input(path);
		if (!input) {
			made.offenders.push_back(path.string() + ": could not be opened");
			continue;
		}
		made.read.push_back(path.string());
		std::string line;
		int number = 0;
		while (std::getline(input, line)) {
			number++;
			const std::string lower = to_lower(line);
			const bool forbidden = names_the_engine(lower) ||
								   (is_include_directive(line) &&
									lower.find("godot") != std::string::npos);
			if (forbidden) {
				made.offenders.push_back(path.string() + ":" + std::to_string(number) + ": " +
										 line);
			}
		}
	}
	return made;
}

bool mentions(const std::vector<std::string> &paths, const std::string &needle) {
	for (const std::string &path : paths) {
		if (path.find(needle) != std::string::npos) {
			return true;
		}
	}
	return false;
}

// The target a `target_link_libraries` line writes INTO: its first argument. `open`, the
// extension, is allowed - and required - to link the engine; the simulation is not.
std::string linked_target(const std::string &lower) {
	const std::size_t call = lower.find("target_link_libraries");
	if (call == std::string::npos) {
		return "";
	}
	std::size_t at = lower.find('(', call);
	if (at == std::string::npos) {
		return "";
	}
	at++;
	while (at < lower.size() && (lower[at] == ' ' || lower[at] == '\t')) {
		at++;
	}
	std::size_t end = at;
	while (end < lower.size() && lower[end] != ' ' && lower[end] != '\t' && lower[end] != ')') {
		end++;
	}
	return lower.substr(at, end - at);
}

std::vector<std::string> scan_link_libraries() {
	std::vector<std::string> offenders;
	const std::filesystem::path cmake = std::filesystem::path(kSourceRoot) / "CMakeLists.txt";
	std::ifstream input(cmake);
	if (!input) {
		offenders.push_back(cmake.string() + ": could not be opened");
		return offenders;
	}
	std::string line;
	int number = 0;
	while (std::getline(input, line)) {
		number++;
		const std::string lower = to_lower(line);
		if (linked_target(lower) == "open_sim" && names_the_engine(lower)) {
			offenders.push_back(cmake.string() + ":" + std::to_string(number) + ": " + line);
		}
	}
	return offenders;
}

void report(const char *what, const std::vector<std::string> &offenders) {
	for (const std::string &offender : offenders) {
		std::cerr << "  " << what << ": " << offender << "\n";
	}
}

}  // namespace

int main() {
	const std::filesystem::path root{kSourceRoot};

	const Scan headers = scan_directory(root / "include");
	const Scan sources = scan_directory(root / "src");
	const std::vector<std::string> links = scan_link_libraries();

	// An empty scan would pass every assertion below, so first prove it opened the very files
	// the contract is about.
	expect_true("the contract read the simulation headers it means to read",
				mentions(headers.read, "open/character.hpp") &&
					mentions(headers.read, "open/combat_session.hpp"));
	expect_true("and the simulation sources it means to read",
				mentions(sources.read, "src/character.cpp") &&
					mentions(sources.read, "src/combat_session.cpp"));
	report("header", headers.offenders);
	report("source", sources.offenders);
	report("link", links);
	expect_true("no simulation header names the engine", headers.offenders.empty());
	expect_true("no simulation source names the engine", sources.offenders.empty());
	expect_true("open_sim links nothing named godot in cpp/CMakeLists.txt", links.empty());

	return test_summary();
}
