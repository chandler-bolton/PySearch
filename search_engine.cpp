// Build (MSVC):   cl /std:c++17 /EHsc /O2 search_engine.cpp
// Build (MinGW):  g++ -std=c++17 -O2 -o search_engine.exe search_engine.cpp
//
// Args: search_engine <folder> <extension> <name> <output_file>
//
// Protocol on stdout (one event per line, flushed):
//   COUNT <n>      progress during the file-count phase
//   TOTAL <n>      file-count phase finished; n is the grand total
//   PROGRESS <n>   progress during the search phase
//   DONE           search finished
//
// Output file format:
//   Found <N> files matching <pattern> in <root> on <YYYY-MM-DD HH:MM:SS>
//   <blank line if N > 0>
//   <path1> <newline if N > 1>
//   <path2> <newline if N > 2>
//   ...

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

// Helper function to easily emit lines to stdout with flushing, to ensure the GUI receives progress updates in a timely manner.
static void emit(const std::string& line) {
    std::cout << line << '\n';
    std::cout.flush();
}

// The actual matching logic -- checks if the filename ends with the specified extension and contains the specified name substring.
static bool ends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

// Single walker shared by the count phase and the search phase, so the two
// phases are guaranteed to visit the same set of files. The `visited` set
// holds canonical paths of directories we've already entered, which prevents
// us from recursing into NTFS junctions, symlinked directories, or any other
// reparse point that resolves to a directory we've already seen.

// The struct that performs the actual walking (search) of the directory tree
// 2 modes based off passed arguments: count phase (just counts total number of items matching criteria) vs search phase (populates "matches" vector with the results)
struct Walker {
    bool find_files;
    bool find_folders;
    std::string extension;
    std::string name;
    std::vector<std::string>* matches;  // nullptr during count phase
    std::uintmax_t processed = 0;
    bool count_phase;
    std::unordered_set<std::string> visited;

    bool enter(const fs::path& dir) {
        std::error_code ec;
        fs::path canon = fs::canonical(dir, ec);
        if (ec) return true;       // can't canonicalize — proceed without cycle check
        return visited.insert(canon.u8string()).second;
    }

    void consider(const fs::path& p) {
        ++processed;
        if (count_phase) {
            if ((processed % 200) == 0) emit("COUNT " + std::to_string(processed));
            return;
        }
        if ((processed % 25) == 0) emit("PROGRESS " + std::to_string(processed));
        const std::string fname = p.filename().u8string();
        if (ends_with(fname, extension) && fname.find(name) != std::string::npos) {
            fs::path np = p;
            np.make_preferred();
            matches->push_back(np.u8string());
        }
    }

    void walk(const fs::path& dir) {
        if (!enter(dir)) return;

        std::error_code ec;
        std::vector<fs::directory_entry> files;
        std::vector<fs::directory_entry> subdirs;
        for (auto& e : fs::directory_iterator(
                 dir, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code stat_ec;
            const fs::file_status sym_st = e.symlink_status(stat_ec);
            if (fs::is_symlink(sym_st)) {
                if (e.is_regular_file(stat_ec)) files.push_back(e);
            } else if (fs::is_directory(sym_st)) {
                subdirs.push_back(e);
            } else if (fs::is_regular_file(sym_st)) {
                files.push_back(e);
            }
        }

        if (find_files) {
            for (auto& e : files) consider(e.path());
        }
        for (auto& e : subdirs) {
            if (find_folders) consider(e.path());
            walk(e.path());
        }
    }
};

// Constructs a human-readable pattern string for the report header, e.g. "*name*extension", "*extension", "*name*", or "*" if both name and extension are empty.
// In other words, returns the pattern/criteria that were actually used for the search
static std::string make_pattern(const std::string& name, const std::string& extension) {
    if (name.empty() && extension.empty()) return "*";
    if (name.empty())      return "*" + extension;
    if (extension.empty()) return "*" + name + "*";
    return "*" + name + "*" + extension;
}

static std::string now_string() {
    std::time_t t = std::time(nullptr);
    std::tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 7) {
        std::cerr << "Usage: search_engine <folder> <extension> <name> <output_file>"
                     " <find_files: 0|1> <find_folders: 0|1>\n";
        return 1;
    }

    // Building arguments; note that we don't do any validation on the extension or name since they can be arbitrary strings, 
    // We do check that the folder exists and is a directory, and that we can write to the output file.
    const fs::path folder = argv[1];
    const std::string extension = argv[2];
    const std::string name = argv[3];
    const fs::path output_file = argv[4];
    const bool find_files = std::string(argv[5]) == "1";
    const bool find_folders = std::string(argv[6]) == "1";

    // Validates the passed starting folder exists and is a directory; if not, we exit out 
    std::error_code ec;
    if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec)) {
        std::cerr << "Folder does not exist: " << argv[1] << "\n";
        return 2;
    }

    // First pass: count the total number of items (files and/or folders) that match the criteria, so we can report progress during the second pass.
    Walker counter{find_files, find_folders, extension, name, nullptr, 0, true, {}};
    counter.walk(folder);
    emit("TOTAL " + std::to_string(counter.processed));

    // Establish the "matches" vector that will be populated during second pass
    // Second pass: perform the actual search and populate the "matches" vector with the results
    std::vector<std::string> matches;
    Walker searcher{find_files, find_folders, extension, name, &matches, 0, false, {}};
    searcher.walk(folder);

    // Establish the output file; exit out if we can't write to it for some reason (e.g. invalid path, permission denied, etc.)
    std::ofstream out(output_file);
    if (!out) {
        std::cerr << "Cannot write to output file: " << output_file.u8string() << "\n";
        return 3;
    }

    // Building human-readable label for the kind of items searched for, e.g. "files", "folders", or "files and folders"
    std::string kind_label;
    if (find_files && find_folders) kind_label = "files and folders";
    else if (find_files)            kind_label = "files";
    else if (find_folders)          kind_label = "folders";
    else                            kind_label = "items";

    // Note: This builds the header line after the search is done, so the elapsed time is included in the report
    const std::string pattern = make_pattern(name, extension);
    fs::path folder_disp = folder;
    folder_disp.make_preferred();
    out << "Found " << matches.size() << " " << kind_label << " matching " << pattern
        << " in " << folder_disp.u8string() << " on " << now_string() << "\n";

    // List matches if any were found
    if (!matches.empty()) {
        out << "\n";
        std::size_t n = 0;
        for (auto& m : matches) out << n++ << ". " << m << "\n\n";
    }

    emit("PROGRESS " + std::to_string(searcher.processed));
    emit("DONE");
    return 0;
}
