CS253 Assignment 1 — Memory-Efficient Versioned File Indexer
Student: Rishitha Roll Number: 230311 Language: C++17 Compile: g++ -std=c++17 -O2 -Wall -Wextra -pedantic -o analyzer 230013_aarin.cpp

Overview
This program implements a memory-efficient versioned file indexer in C++17. It processes large text files incrementally using a fixed-size buffer and supports three analytical query types: word frequency lookup, frequency difference between two versions, and top-K most frequent words.

The core design goal is that memory usage stays independent of file size — only the dictionary of unique words ever grows, not the raw file content.

Compilation
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -o analyzer 230013_aarin.cpp
Requires a C++17-compatible compiler (GCC 7+ or Clang 5+). No external libraries needed.

Usage
Word Count Query
Returns the frequency of a word in a single indexed file.

./analyzer --file dataset_v1.txt --version v1 --buffer 512 --query word --word error
Top-K Query
Displays the K most frequent words in a single indexed file.

./analyzer --file dataset_v1.txt --version v1 --buffer 512 --query top --top 10
Difference Query
Computes the frequency difference of a word between two indexed files.

./analyzer --file1 dataset_v1.txt --version1 v1 \
           --file2 dataset_v2.txt --version2 v2 \
           --buffer 512 --query diff --word error
Command-Line Arguments
Argument	Description
--file <path>	Input file path (word / top queries)
--file1 <path>	First input file (diff query)
--file2 <path>	Second input file (diff query)
--version <n>	Version name (word / top queries)
--version1 <n>	First version name (diff query)
--version2 <n>	Second version name (diff query)
--buffer <kb>	Buffer size in KB — must be 256 to 1024 (default: 512)
--query <type>	Query type: word, diff, or top
--word <token>	Word to look up (word / diff queries)
--top <k>	Number of top results to display (default: 10)
Architecture
The solution is structured around nine classes, each with a single responsibility:

FixedBuffer<T> (Class Template)
A heap-allocated, fixed-capacity buffer backed by std::unique_ptr<T[]>. Non-copyable and movable. Because it uses unique_ptr instead of vector, the capacity is permanently fixed at construction — there is no resize, push_back, or reallocation path.

BufferedFileReader
Opens a file in binary mode and reads it in fixed-size chunks using FixedBuffer. Handles the boundary problem: if a word is split across two consecutive chunks, the partial word at the end of the current chunk is saved in a leftover_ string and prepended to the next chunk before tokenization.

Tokenizer
A stateless parser that scans a string_view chunk and emits lowercase alphanumeric tokens via a user-supplied callback. Provides two overloaded tokenize methods (one with a minLen parameter and one without), demonstrating function overloading.

VersionedIndex
Maintains one unordered_map<string, uint64_t> frequency map per named version. Supports frequency(), diff(), and topK() queries. The topK() method uses std::partial_sort for O(N log K) performance.

Query (Abstract Base Class)
Defines the polymorphic interface with two pure virtual methods: execute(const VersionedIndex&) and description(). All query objects are held as unique_ptr<Query> and dispatched at runtime.

WordQuery, DiffQuery, TopKQuery (Derived Classes)
Each inherits from Query and overrides both virtual methods. Words are normalised to lowercase in each constructor so that matching is always case-insensitive.

QueryProcessor
Owns the Config struct and VersionedIndex. Parses all CLI arguments with full validation, builds version indexes via overloaded indexFile() methods, constructs the correct Query subclass via makeQuery(), executes it, and prints timing.

Design Decisions
Why unique_ptr<char[]> instead of vector<char>? vector<char> can grow silently. unique_ptr<char[]> has no such methods, so the buffer size is provably fixed.

Why callback-based tokenization? Accumulating all tokens into a vector<string> would cause memory to scale with file size. The callback pattern processes each token immediately and discards it.

Why partial_sort for top-K? std::sort is O(N log N). partial_sort is O(N log K) — much faster for small K with large vocabularies.

Why stoll instead of stoul for --top and --buffer? stoul("-1") silently wraps to ULONG_MAX. Using stoll followed by an explicit range check correctly rejects negative inputs.

Memory Model
Component	Memory Usage
Read buffer	Fixed — exactly buffer_kb × 1024 bytes
Leftover string	At most one token length (bounded)
Frequency map	O(unique words × average word length)
Top-K sort	O(unique words) pointer array, freed after query
Total file content	Never stored
Error Handling
All errors throw std::exception subclasses, caught in main, printed as [ERROR] <message>, exit code 1. Every invalid input produces a descriptive message (see report for full table).

Sample Output
======================================================================
Memory-Efficient Versioned File Indexer
======================================================================
Query type : word
Buffer     : 512 KB (524288 bytes)

Indexing 'dataset_v1.txt' as version 'v1' ... done

[Query] word [version=v1, word=error]
  Version   : v1
  Word      : error
  Frequency : 27393

Buffer size (KB): 512
Total execution time (s): 0.024473
