#include "game_create.h"

#include "gfx_bmp.h"
#include "rdt_re1.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace openre
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr int kPixWidth = 320;
        constexpr int kPixHeight = 240;

        // --- hashing ---

        constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
        constexpr uint64_t kFnvPrime = 0x100000001b3ull;

        // FNV-1 64-bit: fast and small rather than cryptographic. Used to
        // verify imported files against known releases of the source game.
        uint64_t fnv1_hash_update(uint64_t hash, const uint8_t* data, size_t size)
        {
            for (size_t i = 0; i < size; i++)
            {
                hash *= kFnvPrime;
                hash ^= data[i];
            }
            return hash;
        }

        uint64_t fnv1_hash(const uint8_t* data, size_t size)
        {
            return fnv1_hash_update(kFnvOffsetBasis, data, size);
        }

        uint64_t fnv1_hash_file(const fs::path& path, bool& ok)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                ok = false;
                return 0;
            }

            uint64_t hash = kFnvOffsetBasis;
            std::vector<char> buffer(64 * 1024);
            while (stream.read(buffer.data(), (std::streamsize)buffer.size()) || stream.gcount() > 0)
            {
                hash = fnv1_hash_update(hash, (const uint8_t*)buffer.data(), (size_t)stream.gcount());
                if (!stream)
                    break;
            }
            ok = true;
            return hash;
        }

        std::string hash_string(uint64_t hash)
        {
            char buf[17];
            std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
            return buf;
        }

        std::string to_lower(std::string s)
        {
            for (auto& c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }

        // --- import tables ---

        enum class ImportAction
        {
            copy,
            pixToBmp,
        };

        struct ImportEntry
        {
            const char* sourcePath; // relative to the source game directory
            const char* destPath;   // relative to the target game pack
            uint64_t expectedHash;  // FNV-1 64 of the known source file
            ImportAction action;
        };

        struct GameDefinition
        {
            const char* id;
            const char* const* signatures; // presence of these identifies the game
            size_t signatureCount;
            const ImportEntry* imports;
            size_t importCount;
        };

        const char* const kRe1Signatures[] = {
            "movie/capcom.avi",
            "sound/bio01.wav",
            "data/title.pix",
        };

        const ImportEntry kRe1Imports[] = {
            { "movie/capcom.avi", "movie/capcom.avi", 0x87c277a448919f85ull, ImportAction::copy },
            { "movie/oj.avi", "movie/pretitle.avi", 0x5b566196fdc0b96bull, ImportAction::copy },
            { "movie/pj.avi", "movie/intro.avi", 0x0953c420d7c813dcull, ImportAction::copy },
            { "sound/bio01.wav", "sound/biohazard.wav", 0x5d2075cb0d8f78fdull, ImportAction::copy },
            { "sound/evil01.wav", "sound/residentevil.wav", 0x8aa2275f5caead03ull, ImportAction::copy },
            { "sound/select01.wav", "sound/select01.wav", 0x474a68c52929deeeull, ImportAction::copy },
            { "sound/select02.wav", "sound/select02.wav", 0xd0a8456c42779015ull, ImportAction::copy },
            { "sound/select03.wav", "sound/select03.wav", 0xc25b8e4e417089efull, ImportAction::copy },
            { "sound/select04.wav", "sound/select04.wav", 0x73b2e1f674d48a5dull, ImportAction::copy },
            { "sound/select05.wav", "sound/select05.wav", 0x514046e5e4574766ull, ImportAction::copy },
            { "sound/select06.wav", "sound/select06.wav", 0x20ab51c8f31b5478ull, ImportAction::copy },
            { "data/title.pix", "texture/title.bmp", 0xaca9bf0f6d376b6eull, ImportAction::pixToBmp },
        };

        const GameDefinition kGames[] = {
            { "re1", kRe1Signatures, std::size(kRe1Signatures), kRe1Imports, std::size(kRe1Imports) },
        };

        // --- source file search ---

        struct Candidate
        {
            fs::path path;     // absolute path of the candidate file
            std::string label; // path relative to the search root, for display
            int priority;      // 0 = <root>/<path>, 1 = <root>/JPN/<path>, 2 = anything else
        };

        // Local mod folders are never treated as source material.
        bool is_pruned_directory(const std::string& nameLower)
        {
            return nameLower.rfind("mod_", 0) == 0 || nameLower.rfind("_mod_", 0) == 0;
        }

        // Guards against pathological trees (e.g. junction loops).
        constexpr int kMaxSearchDepth = 16;

        // Single-pass source file index. One depth-first walk of the (often
        // huge) source tree resolves every wanted relative path at once,
        // case-insensitively. A wanted path is matched by any directory
        // nesting whose remaining suffix equals it, e.g. "movie/capcom.avi"
        // is found at "<root>/movie/capcom.avi", "<root>/JPN/movie/capcom.avi"
        // or deeper. Directories named "mod_*" / "_mod_*" are never searched.
        class SourceFileIndex
        {
        public:
            SourceFileIndex(const fs::path& root, const std::vector<std::string>& wantedPaths)
            {
                for (const auto& path : wantedPaths)
                    _wanted[to_lower(path)];

                collect(root, "", "", 0);

                for (auto& [key, candidates] : _wanted)
                {
                    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                        return a.priority < b.priority;
                    });
                }
            }

            const std::vector<Candidate>& candidates(const std::string& relPath) const
            {
                const auto it = _wanted.find(to_lower(relPath));
                return it == _wanted.end() ? s_empty : it->second;
            }

        private:
            using WantedMap = std::map<std::string, std::vector<Candidate>>;

            static const std::vector<Candidate> s_empty;

            void add(WantedMap::iterator slot, const fs::path& path, const std::string& display, int priority)
            {
                slot->second.push_back(Candidate{ path, display, priority });
            }

            void try_match(const fs::path& path, const std::string& relLower, const std::string& relDisplay)
            {
                // Exact match directly under the root.
                {
                    const auto exact = _wanted.find(relLower);
                    if (exact != _wanted.end())
                        add(exact, path, relDisplay, 0);
                }

                // Suffixes below nested directories ("jpn/<path>", anything
                // deeper).
                for (size_t pos = relLower.find('/'); pos != std::string::npos; pos = relLower.find('/', pos + 1))
                {
                    const std::string suffix = relLower.substr(pos + 1);
                    const auto slot = _wanted.find(suffix);
                    if (slot == _wanted.end())
                        continue;
                    const bool underJpn = relLower.compare(0, 4, "jpn/") == 0;
                    add(slot, path, relDisplay, underJpn ? 1 : 2);
                }
            }

            void collect(const fs::path& dir, const std::string& prefixLower, const std::string& prefixDisplay, int depth)
            {
                if (depth > kMaxSearchDepth)
                    return;

                std::error_code ec;
                fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
                if (ec)
                    return;
                const fs::directory_iterator end;

                std::vector<fs::path> entries;
                while (it != end)
                {
                    entries.push_back(it->path());
                    it.increment(ec);
                    if (ec)
                        break;
                }

                std::sort(entries.begin(), entries.end());

                for (const auto& path : entries)
                {
                    // Trees can contain paths not representable in the current
                    // code page; those can never match an ASCII table entry,
                    // so skip them rather than fail the search.
                    std::error_code typeEc;
                    std::string nameLower;
                    std::string nameDisplay;
                    try
                    {
                        nameDisplay = path.filename().string();
                        nameLower = to_lower(nameDisplay);
                    }
                    catch (...)
                    {
                        continue;
                    }

                    const std::string childLower = prefixLower + nameLower;
                    const std::string childDisplay = prefixDisplay + nameDisplay;

                    if (fs::is_directory(path, typeEc))
                    {
                        if (is_pruned_directory(nameLower))
                            continue;
                        collect(path, childLower + "/", childDisplay + "/", depth + 1);
                    }
                    else
                    {
                        try_match(path, childLower, childDisplay);
                    }
                }
            }

            WantedMap _wanted;
        };

        const std::vector<Candidate> SourceFileIndex::s_empty;

        // Picks the best candidate for an import entry: a file whose hash
        // matches the expected value always wins over positional priority.
        // Without a hash match the highest-priority candidate is used.
        fs::path select_candidate(const std::vector<Candidate>& candidates, uint64_t expectedHash, std::string& outLabel)
        {
            for (const auto& candidate : candidates)
            {
                bool ok = false;
                const uint64_t hash = fnv1_hash_file(candidate.path, ok);
                if (ok && hash == expectedHash)
                {
                    outLabel = candidate.label;
                    return candidate.path;
                }
            }
            outLabel = candidates.front().label;
            return candidates.front().path;
        }

        // --- detection and scan ---

        enum class EntryStatus
        {
            verified,
            mismatch,
            missing,
        };

        struct ScanResult
        {
            const ImportEntry* entry;
            EntryStatus status;
            fs::path sourcePath; // absolute path actually used
            std::string sourceLabel;
            uint64_t actualHash = 0;
        };

        // True when every signature of the game is present somewhere in the
        // indexed source tree.
        bool game_matches(const GameDefinition& game, const SourceFileIndex& index)
        {
            for (size_t i = 0; i < game.signatureCount; i++)
            {
                if (index.candidates(game.signatures[i]).empty())
                    return false;
            }
            return true;
        }

        void print_usage()
        {
            std::cerr << "usage: openre.exe create-game <target-pack-dir> --source <original-game-dir>\n";
        }

        bool stdin_isatty()
        {
#ifdef _WIN32
            return _isatty(_fileno(stdin)) != 0;
#else
            return isatty(fileno(stdin)) != 0;
#endif
        }

        // Copies `source` to `dest` in chunks, warning when the copied bytes
        // no longer match the expected hash.
        bool copy_file_checked(const fs::path& source, const fs::path& dest, uint64_t expectedHash)
        {
            std::ifstream in(source, std::ios::binary);
            std::ofstream out(dest, std::ios::binary);
            if (!in || !out)
                return false;

            uint64_t hash = kFnvOffsetBasis;
            std::vector<char> buffer(256 * 1024);
            while (in.read(buffer.data(), (std::streamsize)buffer.size()) || in.gcount() > 0)
            {
                const auto got = (std::streamsize)in.gcount();
                out.write(buffer.data(), got);
                hash = fnv1_hash_update(hash, (const uint8_t*)buffer.data(), (size_t)got);
                if (!in)
                    break;
            }
            if (!out)
                return false;

            if (hash != expectedHash)
            {
                std::cout << "  warn  " << dest.filename().string() << " written but hash changed during copy (expected "
                          << hash_string(expectedHash) << ", got " << hash_string(hash) << ")\n";
            }
            return true;
        }

        // Converts a raw RGB555 .pix image into a BMP at `dest`.
        bool convert_pix_to_bmp(const fs::path& source, const fs::path& dest)
        {
            std::ifstream in(source, std::ios::binary);
            if (!in)
                return false;
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const size_t expectedSize = (size_t)kPixWidth * kPixHeight * 2;
            if (bytes.size() != expectedSize)
            {
                std::cout << "  warn  " << source.string() << ": unexpected size " << bytes.size() << " bytes (expected "
                          << expectedSize << "), skipping\n";
                return false;
            }
            return graphics::bmp_write_rgb555(dest.string().c_str(), kPixWidth, kPixHeight, (const uint16_t*)bytes.data());
        }

        // Reads a whole file into a byte vector (empty on failure).
        std::vector<uint8_t> read_file_bytes(const fs::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return {};
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return bytes;
        }

        // --- RE1 room conversion ---

        // Recursively finds RE1 room files (STAGE*/ROOM*.RDT) in the source
        // tree, skipping local mod folders like the asset index does.
        std::vector<fs::path> find_re1_rooms(const fs::path& root)
        {
            std::vector<fs::path> rooms;
            std::error_code ec;
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator end;
            while (it != end)
            {
                std::error_code typeEc;
                if (it->is_regular_file(typeEc) && it->path().extension() == ".RDT")
                {
                    const std::string name = to_lower(it->path().filename().string());
                    if (name.rfind("room", 0) == 0 && name.size() > 4 && isxdigit((unsigned char)name[4]))
                    {
                        rooms.push_back(it->path());
                    }
                }
                else if (it->is_directory(typeEc) && is_pruned_directory(to_lower(it->path().filename().string())))
                {
                    it.disable_recursion_pending();
                }
                it.increment(ec);
                if (ec)
                    break;
            }
            std::sort(rooms.begin(), rooms.end());
            return rooms;
        }

        // Converts an RE1 room file into RE2 RDT format and writes it to the
        // game pack under room/<roomcode>.rdt. Returns the RE2 room code (e.g.
        // "room1000") or empty on failure.
        std::string convert_re1_room(const fs::path& source, const fs::path& targetRoot, bool& ok)
        {
            ok = false;
            auto bytes = read_file_bytes(source);
            if (bytes.empty())
                return {};

            auto converted = rdt_re1::convert_re1_rdt(bytes.data(), bytes.size());
            if (converted.empty())
                return {};

            // Reuse the RE1 file name (e.g. ROOM1000.RDT) as the RE2 room
            // code; the engine loads rooms from PlX\Rdt\roomXXXX.rdt.
            const std::string name = source.filename().string();
            const std::string code = to_lower(name.substr(0, name.size() - 4));
            const fs::path dest = targetRoot / "room" / (code + ".rdt");

            std::error_code destEc;
            fs::create_directories(dest.parent_path(), destEc);
            if (destEc)
                return {};

            std::ofstream out(dest, std::ios::binary);
            if (!out)
                return {};
            out.write((const char*)converted.data(), (std::streamsize)converted.size());
            if (!out)
                return {};

            ok = true;
            return code;
        }

        // --- RE1 cut backgrounds (STAGE*/RC*.pak) ---

        // An RC pak filename encodes the room code and cut directly:
        // RC<room3><cut>.pak, e.g. RC1011.pak = room 101, cut 1 (shared by
        // the ROOM1010/ROOM1011 pair). Scan them straight from the tree
        // (pruning mod folders) rather than pairing via the RDTs.
        struct Re1Background
        {
            fs::path path;
            std::string roomCode; // 3 lowercase hex digits, e.g. "101"
            int cut;              // 0..15
        };

        std::vector<Re1Background> find_re1_backgrounds(const fs::path& root)
        {
            std::vector<Re1Background> backgrounds;
            std::error_code ec;
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator end;
            while (it != end)
            {
                std::error_code typeEc;
                if (it->is_regular_file(typeEc) && it->path().extension() == ".pak")
                {
                    const std::string name = it->path().filename().string();
                    if (name.size() == 10 && name.rfind("RC", 0) == 0)
                    {
                        const std::string code = to_lower(name.substr(2, 3));
                        const int cut = static_cast<int>(strtol(name.substr(5, 1).c_str(), nullptr, 16));
                        if (code.size() == 3 && std::all_of(code.begin(), code.end(), [](char c) {
                                return isxdigit((unsigned char)c);
                            }))
                        {
                            backgrounds.push_back(Re1Background{ it->path(), code, cut });
                        }
                    }
                }
                else if (it->is_directory(typeEc) && is_pruned_directory(to_lower(it->path().filename().string())))
                {
                    it.disable_recursion_pending();
                }
                it.increment(ec);
                if (ec)
                    break;
            }
            std::sort(backgrounds.begin(), backgrounds.end(), [](const Re1Background& a, const Re1Background& b) {
                return std::tie(a.roomCode, a.cut) < std::tie(b.roomCode, b.cut);
            });
            return backgrounds;
        }

        // Decompresses an RE1 cut background pak, converts the embedded 16bpp
        // TIM to RGB555 and writes it as a BMP under roomcut/<room>_<cut>.bmp.
        // Camera cuts are 320x240; paks of other sizes (e.g. the one-off
        // 316x236 status screen) are not room backgrounds and are skipped.
        // Returns 1 on success, 0 when skipped, -1 on failure.
        int convert_re1_background(const Re1Background& bg, const fs::path& targetRoot)
        {
            auto bytes = read_file_bytes(bg.path);
            if (bytes.empty())
                return -1;

            std::vector<uint8_t> rgb555;
            int w = 0;
            int h = 0;
            if (!rdt_re1::convert_re1_background(bytes.data(), bytes.size(), rgb555, w, h))
                return -1;
            if (w != 320 || h != 240)
                return 0;

            char name[16];
            std::snprintf(name, sizeof(name), "%s_%02x.bmp", bg.roomCode.c_str(), bg.cut);
            const fs::path dest = targetRoot / "roomcut" / name;

            std::error_code destEc;
            fs::create_directories(dest.parent_path(), destEc);
            if (destEc)
                return -1;

            return graphics::bmp_write_rgb555(
                       dest.string().c_str(), 320, 240, reinterpret_cast<const uint16_t*>(rgb555.data()))
                ? 1
                : -1;
        }
    }

    bool game_create_requested(const char** args, int argc)
    {
        return args != nullptr && argc > 1 && args[1] != nullptr && std::strcmp(args[1], "create-game") == 0;
    }

    static int game_create_run(const char** args, int argc);

    int game_create_main(const char** args, int argc)
    {
        // Filesystem helpers throw on unexpected errors (long paths, locked
        // files, ...); report instead of aborting the process.
        try
        {
            return game_create_run(args, argc);
        }
        catch (const std::exception& e)
        {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
    }

    // Parses arguments, detects the game and runs the import.
    static int game_create_run(const char** args, int argc)
    {
        std::string targetArg;
        std::string sourceArg;
        bool hasSource = false;

        for (int i = 2; i < argc; i++)
        {
            const std::string arg = args[i] ? args[i] : "";
            if (arg == "--source")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "error: --source requires a directory\n";
                    print_usage();
                    return 1;
                }
                sourceArg = args[++i];
                hasSource = true;
            }
            else if (arg.rfind("--source=", 0) == 0)
            {
                sourceArg = arg.substr(9);
                hasSource = true;
            }
            else if (!arg.empty() && arg[0] != '-')
            {
                if (!targetArg.empty())
                {
                    std::cerr << "error: unexpected extra argument '" << arg << "'\n";
                    print_usage();
                    return 1;
                }
                targetArg = arg;
            }
            else
            {
                std::cerr << "error: unknown option '" << arg << "'\n";
                print_usage();
                return 1;
            }
        }

        if (targetArg.empty() || !hasSource)
        {
            print_usage();
            return 1;
        }

        const fs::path sourceRoot{ sourceArg };
        if (!fs::is_directory(sourceRoot))
        {
            std::cerr << "error: source directory does not exist: " << sourceArg << "\n";
            return 1;
        }

        // One walk of the source tree resolves every signature and import
        // entry of every registered game.
        std::vector<std::string> wantedPaths;
        for (const auto& game : kGames)
        {
            for (size_t i = 0; i < game.signatureCount; i++)
                wantedPaths.push_back(game.signatures[i]);
            for (size_t i = 0; i < game.importCount; i++)
                wantedPaths.push_back(game.imports[i].sourcePath);
        }
        SourceFileIndex index(sourceRoot, wantedPaths);

        // Detect the game from the presence of its signature files.
        std::vector<const GameDefinition*> detected;
        for (const auto& game : kGames)
        {
            if (game_matches(game, index))
                detected.push_back(&game);
        }

        if (detected.empty())
        {
            std::cerr << "error: could not recognise a supported game in '" << sourceArg << "'\n";
            return 1;
        }
        if (detected.size() > 1)
        {
            std::cerr << "error: source directory matches multiple games:";
            for (const auto* game : detected)
                std::cerr << " " << game->id;
            std::cerr << "\n";
            return 1;
        }

        const GameDefinition* game = detected.front();
        const fs::path targetRoot{ targetArg };

        // Phase 1: gather and verify every import entry up front.
        std::vector<ScanResult> results;
        size_t verified = 0;
        size_t mismatched = 0;
        size_t missing = 0;
        for (size_t i = 0; i < game->importCount; i++)
        {
            const ImportEntry* entry = &game->imports[i];

            ScanResult result;
            result.entry = entry;
            result.status = EntryStatus::missing;

            auto candidates = index.candidates(entry->sourcePath);
            if (!candidates.empty())
            {
                result.sourcePath = select_candidate(candidates, entry->expectedHash, result.sourceLabel);
                bool ok = false;
                result.actualHash = fnv1_hash_file(result.sourcePath, ok);
                result.status
                    = (ok && result.actualHash == entry->expectedHash) ? EntryStatus::verified : EntryStatus::mismatch;
            }

            switch (result.status)
            {
            case EntryStatus::verified: verified++; break;
            case EntryStatus::mismatch: mismatched++; break;
            case EntryStatus::missing: missing++; break;
            }
            results.push_back(result);
        }

        // Phase 2: report, then confirm before touching the target.
        std::cout << "create-game: detected game '" << game->id << "'\n";
        std::cout << "source: " << fs::absolute(sourceRoot).string() << "\n";
        std::cout << "target: " << fs::absolute(targetRoot).string() << "\n\n";

        size_t existingTargets = 0;
        for (const auto& result : results)
        {
            std::error_code existsEc;
            const bool exists = fs::exists(targetRoot / result.entry->destPath, existsEc);
            existingTargets += exists ? 1 : 0;

            switch (result.status)
            {
            case EntryStatus::verified:
                std::cout << "  ok    " << result.entry->destPath << " <- " << result.sourceLabel << "\n";
                break;
            case EntryStatus::mismatch:
                std::cout << "  warn  " << result.entry->destPath << " <- " << result.sourceLabel
                          << " (hash mismatch: expected " << hash_string(result.entry->expectedHash) << ", got "
                          << hash_string(result.actualHash) << ")\n";
                break;
            case EntryStatus::missing:
                std::cout << "  warn  " << result.entry->destPath << " <- " << result.entry->sourcePath << " (missing)\n";
                break;
            }
            if (exists)
                std::cout << "        (existing file will be overwritten)\n";
        }

        std::cout << "\n"
                  << results.size() << " entries: " << verified << " verified, " << mismatched << " hash mismatches, "
                  << missing << " missing\n";

        if (verified + mismatched == 0)
        {
            std::cerr << "error: none of the expected files could be found in '" << sourceArg << "'\n";
            return 1;
        }
        if (existingTargets > 0)
            std::cout << existingTargets << " existing file(s) will be overwritten\n";

        if (stdin_isatty())
        {
            std::cout << "\nProceed with import? [y/N] ";
            std::cout.flush();
            std::string line;
            if (!std::getline(std::cin, line))
                line.clear();
            const std::string answer = to_lower(line);
            if (answer != "y" && answer != "yes")
            {
                std::cout << "aborted.\n";
                return 1;
            }
        }
        else
        {
            std::cout << "no tty attached, proceeding automatically\n";
        }

        // Execute.
        std::error_code mkDirEc;
        fs::create_directories(targetRoot, mkDirEc);
        if (mkDirEc)
        {
            std::cerr << "error: could not create target directory '" << targetArg << "': " << mkDirEc.message() << "\n";
            return 1;
        }

        size_t imported = 0;
        size_t failures = 0;
        for (const auto& result : results)
        {
            if (result.status == EntryStatus::missing)
                continue;

            const fs::path dest = targetRoot / result.entry->destPath;
            std::error_code destEc;
            fs::create_directories(dest.parent_path(), destEc);
            if (destEc && !fs::is_directory(dest.parent_path(), destEc))
            {
                failures++;
                std::cout << "  fail  " << result.entry->destPath << " (" << destEc.message() << ")\n";
                continue;
            }

            bool ok = false;
            if (result.entry->action == ImportAction::pixToBmp)
                ok = convert_pix_to_bmp(result.sourcePath, dest);
            else
                ok = copy_file_checked(result.sourcePath, dest, result.entry->expectedHash);

            if (ok)
            {
                imported++;
                std::cout << "  done  " << result.entry->destPath << "\n";
            }
            else
            {
                failures++;
                std::cout << "  fail  " << result.entry->destPath << "\n";
            }
        }

        std::cout << "\nimported " << imported << "/" << results.size() << " entries into " << fs::absolute(targetRoot).string()
                  << "\n";

        // RE1 rooms: convert every STAGE*/ROOM*.RDT into an RE2 RDT and place
        // it in the game pack so the engine can load it as a room.
        size_t roomsConverted = 0;
        size_t roomFailures = 0;
        if (std::strcmp(game->id, "re1") == 0)
        {
            auto rooms = find_re1_rooms(sourceRoot);
            std::cout << "converting " << rooms.size() << " RE1 rooms...\n";
            for (const auto& room : rooms)
            {
                bool ok = false;
                const std::string code = convert_re1_room(room, targetRoot, ok);
                if (ok)
                {
                    roomsConverted++;
                    std::cout << "  done  " << code << ".rdt\n";
                }
                else
                {
                    roomFailures++;
                    std::cout << "  fail  " << room.filename().string() << "\n";
                }
            }
            std::cout << "converted " << roomsConverted << "/" << rooms.size() << " rooms\n";

            // RE1 cut backgrounds: decompress every STAGE*/RC*.pak and write
            // it as a BMP under roomcut/<room>_<cut>.bmp.
            size_t bgsConverted = 0;
            size_t bgFailures = 0;
            size_t bgSkipped = 0;
            auto backgrounds = find_re1_backgrounds(sourceRoot);
            std::cout << "converting " << backgrounds.size() << " RE1 cut backgrounds...\n";
            for (const auto& bg : backgrounds)
            {
                const int result = convert_re1_background(bg, targetRoot);
                if (result > 0)
                {
                    bgsConverted++;
                    std::cout << "  done  " << bg.roomCode << "_" << bg.cut << ".bmp\n";
                }
                else if (result == 0)
                {
                    bgSkipped++;
                    std::cout << "  skip  " << bg.path.filename().string() << " (not 320x240)\n";
                }
                else
                {
                    bgFailures++;
                    std::cout << "  fail  " << bg.path.filename().string() << "\n";
                }
            }
            std::cout << "converted " << bgsConverted << "/" << backgrounds.size() << " cut backgrounds"
                      << (bgSkipped ? " (" + std::to_string(bgSkipped) + " skipped)" : "") << "\n";
            roomFailures += bgFailures;
        }

        return failures == 0 && roomFailures == 0 ? 0 : 1;
    }
}
