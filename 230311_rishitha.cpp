// =============================================================================
//  230311-Rishitha.cpp
//  Student  : Aarin Sheth  (Roll No. 230013)
//  CS253 Assignment 1 — Memory-Efficient Versioned File Indexer
//  Compile  : g++ -std=c++17 -O2 -Wall -Wextra -o analyzer 230013_aarin.cpp
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <iomanip>

using namespace std;

// =============================================================================
//  FixedBuffer<T>  —  User-defined class template
//  A fixed-size heap buffer that cannot be resized after construction.
//  Using a raw array (new T[]) instead of vector so the size is truly fixed.
// =============================================================================
template <typename T>
class FixedBuffer
{
public:
    explicit FixedBuffer(size_t size)
    {
        if (size == 0)
            throw invalid_argument("FixedBuffer: size must be > 0");
        bufSize = size;
        data = new T[bufSize];
    }

    FixedBuffer(const FixedBuffer&)            = delete;
    FixedBuffer& operator=(const FixedBuffer&) = delete;

    ~FixedBuffer() { delete[] data; }

    T*     getData() { return data; }
    size_t getSize() { return bufSize; }

private:
    T*     data;
    size_t bufSize;
};

// =============================================================================
//  BufferedFileReader  —  Reads a file in fixed-size chunks
//  Handles words split across chunk boundaries using a leftover string.
// =============================================================================
class BufferedFileReader
{
public:
    BufferedFileReader(const string& path, size_t bufferBytes)
        : buffer(bufferBytes)
    {
        file.open(path, ios::binary);
        if (!file.is_open())
            throw runtime_error("Cannot open file: " + path);
    }

    bool readChunk(string& outData)
    {
        if (exhausted && leftover.empty())
            return false;

        outData.clear();

        if (!leftover.empty())
            outData.swap(leftover);

        if (exhausted)
            return !outData.empty();

        file.read(buffer.getData(), static_cast<streamsize>(buffer.getSize()));
        streamsize bytesRead = file.gcount();

        if (bytesRead <= 0)
        {
            exhausted = true;
            return !outData.empty();
        }

        outData.append(buffer.getData(), static_cast<size_t>(bytesRead));

        if (file.eof())
        {
            exhausted = true;
            return true;
        }

        // Scan backward to find the last non-alphanumeric character.
        // Everything after it is a partial word — save for next call.
        size_t pos = outData.size();
        while (pos > 0 && isalnum(static_cast<unsigned char>(outData[pos - 1])))
            pos--;

        if (pos < outData.size())
        {
            leftover = outData.substr(pos);
            outData.resize(pos);
        }
        else
        {
            // Entire buffer is one unfinished word — keep reading
            leftover.swap(outData);
            outData.clear();
        }

        if (!outData.empty())
            return true;

        return readChunk(outData);  // recurse to get more data
    }

private:
    FixedBuffer<char> buffer;
    ifstream          file;
    string            leftover;
    bool              exhausted = false;
};

// =============================================================================
//  Tokenizer  —  Stateless word extractor
//  Two overloaded tokenize() methods demonstrate function overloading.
// =============================================================================
class Tokenizer
{
public:
    // Overload 1 — default minimum word length = 1
    template <typename Callback>
    void tokenize(const string& data, Callback callback) const
    {
        tokenize(data, 1, callback);
    }

    // Overload 2 — explicit minimum word length
    template <typename Callback>
    void tokenize(const string& data, size_t minLen, Callback callback) const
    {
        string word;
        word.reserve(32);

        for (size_t i = 0; i <= data.size(); i++)
        {
            char ch = (i < data.size()) ? data[i] : '\0';

            if (i < data.size() && isalnum(static_cast<unsigned char>(ch)))
            {
                word += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            }
            else
            {
                if (word.size() >= minLen)
                    callback(word);
                word.clear();
            }
        }
    }
};

// =============================================================================
//  VersionedIndex  —  Stores one frequency map per version name
// =============================================================================
class VersionedIndex
{
public:
    using FreqMap = unordered_map<string, size_t>;

    void buildIndex(const string& version,
                    BufferedFileReader& reader,
                    const Tokenizer& tokenizer)
    {
        if (indexes.count(version))
            throw runtime_error("Version already indexed: " + version);

        FreqMap& freqMap = indexes[version];
        string chunk;

        while (reader.readChunk(chunk))
        {
            // Call the 2-argument overload explicitly
            tokenizer.tokenize(chunk, 1UL, [&freqMap](const string& word)
            {
                freqMap[word]++;
            });
        }
    }

    size_t getFrequency(const string& version, const string& word) const
    {
        auto it = indexes.find(version);
        if (it == indexes.end())
            throw runtime_error("Unknown version: " + version);
        auto wt = it->second.find(word);
        return (wt != it->second.end()) ? wt->second : 0;
    }

    const FreqMap& getMap(const string& version) const
    {
        auto it = indexes.find(version);
        if (it == indexes.end())
            throw runtime_error("Unknown version: " + version);
        return it->second;
    }

private:
    unordered_map<string, FreqMap> indexes;
};

// =============================================================================
//  Query (abstract base class) + three derived query classes
// =============================================================================
class Query
{
public:
    virtual void execute(const VersionedIndex& idx) const = 0;
    virtual string description() const = 0;
    virtual ~Query() {}
};

// Derived 1: WordQuery
class WordQuery : public Query
{
public:
    WordQuery(const string& ver, const string& w) : version(ver), word(w) {}

    void execute(const VersionedIndex& idx) const override
    {
        cout << "  Version   : " << version << "\n"
             << "  Word      : " << word << "\n"
             << "  Frequency : " << idx.getFrequency(version, word) << "\n";
    }

    string description() const override
    {
        return "word [version=" + version + ", word=" + word + "]";
    }

private:
    string version, word;
};

// Derived 2: DiffQuery
class DiffQuery : public Query
{
public:
    DiffQuery(const string& v1, const string& v2, const string& w)
        : version1(v1), version2(v2), word(w) {}

    void execute(const VersionedIndex& idx) const override
    {
        size_t f1    = idx.getFrequency(version1, word);
        size_t f2    = idx.getFrequency(version2, word);
        long   delta = static_cast<long>(f1) - static_cast<long>(f2);

        cout << "  Version1  : " << version1 << " (freq = " << f1 << ")\n"
             << "  Version2  : " << version2 << " (freq = " << f2 << ")\n"
             << "  Word      : " << word << "\n"
             << "  Difference: " << (delta >= 0 ? "+" : "") << delta
             << " (" << version1 << " - " << version2 << ")\n";
    }

    string description() const override
    {
        return "diff [v1=" + version1 + ", v2=" + version2 + ", word=" + word + "]";
    }

private:
    string version1, version2, word;
};

// Derived 3: TopKQuery
class TopKQuery : public Query
{
public:
    TopKQuery(const string& ver, size_t k) : version(ver), topK(k) {}

    void execute(const VersionedIndex& idx) const override
    {
        const VersionedIndex::FreqMap& freqMap = idx.getMap(version);

        vector<pair<string, size_t>> words(freqMap.begin(), freqMap.end());

        sort(words.begin(), words.end(),
             [](const pair<string, size_t>& a, const pair<string, size_t>& b)
             {
                 if (a.second != b.second) return a.second > b.second;
                 return a.first < b.first;
             });

        size_t limit = min(topK, words.size());

        cout << "  Version   : " << version << "\n"
             << "  Top-K     : " << topK << "\n";

        for (size_t i = 0; i < limit; i++)
        {
            cout << "    " << setw(4) << right << (i + 1)
                 << ". " << setw(28) << left << words[i].first
                 << " " << words[i].second << "\n";
        }
    }

    string description() const override
    {
        return "top [version=" + version + ", k=" + to_string(topK) + "]";
    }

private:
    string version;
    size_t topK;
};

// =============================================================================
//  QueryProcessor  —  Parses CLI, builds indexes, runs query, prints timing
// =============================================================================
class QueryProcessor
{
public:
    QueryProcessor(int argc, char* argv[])
    {
        parseArgs(argc, argv);
    }

    void run()
    {
        using Clock = chrono::high_resolution_clock;
        auto start = Clock::now();

        printBanner();

        Tokenizer tokenizer;

        if (queryType == "word" || queryType == "top")
        {
            indexFile(file, version, tokenizer);              // 3-arg overload
        }
        else
        {
            indexFile(file1, version1, bufferBytes, tokenizer); // 4-arg overload
            indexFile(file2, version2, bufferBytes, tokenizer); // 4-arg overload
        }

        unique_ptr<Query> query = makeQuery();

        cout << "\n[Query] " << query->description() << "\n";
        query->execute(index);

        double elapsed = chrono::duration<double>(Clock::now() - start).count();
        cout << "\nBuffer size (KB): " << bufferKB << "\n"
             << "Total execution time (s): "
             << fixed << setprecision(6) << elapsed << "\n";
    }

private:
    string file, file1, file2;
    string version, version1, version2;
    string queryType, word;
    size_t bufferKB    = 512;
    size_t bufferBytes = 512 * 1024;
    size_t topK        = 10;

    VersionedIndex index;

    // Safe integer parser — rejects negatives and non-numeric strings
    long long parseNumber(const string& text, const string& flag)
    {
        size_t pos = 0;
        long long val = 0;
        try
        {
            val = stoll(text, &pos);
        }
        catch (const invalid_argument&)
        {
            throw runtime_error(flag + " requires a numeric value, got: " + text);
        }
        catch (const out_of_range&)
        {
            throw runtime_error(flag + " value out of range: " + text);
        }
        if (pos != text.size())
            throw runtime_error(flag + " requires a pure integer, got: " + text);
        return val;
    }

    void parseArgs(int argc, char* argv[])
    {
        for (int i = 1; i < argc; i++)
        {
            string arg = argv[i];

            auto next = [&]() -> string {
                if (i + 1 >= argc)
                    throw runtime_error("Missing value after " + arg);
                return string(argv[++i]);
            };

            if      (arg == "--file")     file      = next();
            else if (arg == "--file1")    file1     = next();
            else if (arg == "--file2")    file2     = next();
            else if (arg == "--version")  version   = next();
            else if (arg == "--version1") version1  = next();
            else if (arg == "--version2") version2  = next();
            else if (arg == "--query")    queryType = next();
            else if (arg == "--word")     word      = next();
            else if (arg == "--top")
            {
                string v = next();
                long long p = parseNumber(v, "--top");
                if (p <= 0)
                    throw runtime_error("--top must be >= 1, got: " + v);
                topK = static_cast<size_t>(p);
            }
            else if (arg == "--buffer")
            {
                string v = next();
                long long p = parseNumber(v, "--buffer");
                if (p < 256 || p > 1024)
                    throw runtime_error("--buffer must be between 256 and 1024 KB, got: " + v);
                bufferKB    = static_cast<size_t>(p);
                bufferBytes = bufferKB * 1024;
            }
            else
            {
                throw runtime_error("Unknown argument: " + arg);
            }
        }

        validateConfig();
    }

    void validateConfig()
    {
        if (queryType != "word" && queryType != "diff" && queryType != "top")
            throw runtime_error("--query must be one of: word | diff | top");

        if (queryType == "word")
        {
            if (file.empty() || version.empty() || word.empty())
                throw runtime_error("word query requires --file --version --word");
        }
        else if (queryType == "top")
        {
            if (file.empty() || version.empty())
                throw runtime_error("top query requires --file --version");
        }
        else  // diff
        {
            if (version1 == version2)
                throw runtime_error("diff query: --version1 and --version2 must be different");
            if (file1.empty() || version1.empty() || file2.empty() ||
                version2.empty() || word.empty())
                throw runtime_error("diff query requires --file1 --version1 --file2 --version2 --word");
        }
    }

    // Overload A: 3-argument — uses configured buffer size
    void indexFile(const string& path, const string& ver, const Tokenizer& tok)
    {
        indexFile(path, ver, bufferBytes, tok);
    }

    // Overload B: 4-argument — explicit buffer size
    void indexFile(const string& path, const string& ver,
                   size_t bytes, const Tokenizer& tok)
    {
        cout << "Indexing '" << path << "' as version '" << ver << "' ... ";
        cout.flush();
        BufferedFileReader reader(path, bytes);
        index.buildIndex(ver, reader, tok);
        cout << "done\n";
    }

    unique_ptr<Query> makeQuery()
    {
        // Normalise word to lowercase
        for (char& c : word)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        if (queryType == "word") return make_unique<WordQuery>(version, word);
        if (queryType == "diff") return make_unique<DiffQuery>(version1, version2, word);
        if (queryType == "top")  return make_unique<TopKQuery>(version, topK);
        throw logic_error("Unreachable after validation");
    }

    void printBanner()
    {
        string sep(70, '=');
        cout << sep << "\n"
             << "Memory-Efficient Versioned File Indexer\n"
             << "Aarin Sheth | Roll No. 230013\n"
             << sep << "\n"
             << "Query type : " << queryType << "\n"
             << "Buffer     : " << bufferKB << " KB ("
             << bufferBytes << " bytes)\n\n";
    }
};

// =============================================================================
//  main
// =============================================================================
static void printUsage(const char* prog)
{
    cerr << "Usage:\n"
         << "  " << prog << " --file <path> --version <n> --buffer <kb>"
                            " --query word --word <token>\n"
         << "  " << prog << " --file <path> --version <n> --buffer <kb>"
                            " --query top --top <k>\n"
         << "  " << prog << " --file1 <path> --version1 <n>"
                            " --file2 <path> --version2 <n>"
                            " --buffer <kb> --query diff --word <token>\n\n"
         << "  --buffer  256 to 1024 KB  (default 512)\n"
         << "  --top     >= 1            (default 10)\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    try
    {
        QueryProcessor processor(argc, argv);
        processor.run();
        return 0;
    }
    catch (const exception& e)
    {
        cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}
