#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;
constexpr double ERROR_RATE = 1e-6;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        }
        else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status,
        const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status });
    }

    template <typename Predicate>
    vector<Document> FindTopDocuments(const string& raw_query, Predicate predicate) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, predicate);

        sort(matched_documents.begin(), matched_documents.end(),
            [](const Document& lhs, const Document& rhs) {
                if (abs(lhs.relevance - rhs.relevance) < ERROR_RATE) {
                    return lhs.rating > rhs.rating;
                }
                else {
                    return lhs.relevance > rhs.relevance;
                }
            });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        vector<Document> matched_documents = FindTopDocuments(
            raw_query, [status](int document_id, DocumentStatus doc_status, int rating) {
                return doc_status == status;
            }
        );
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }


    int GetDocumentCount() const {
        return documents_.size();
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query,
        int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return { matched_words, documents_.at(document_id).status };
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = accumulate(ratings.begin(), ratings.end(), 0);
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return { text, is_minus, IsStopWord(text) };
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                }
                else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template <typename Predicate>
    vector<Document> FindAllDocuments(const Query& query, Predicate predicate) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (predicate(document_id, document_data.status, document_data.rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back(
                { document_id, relevance, documents_.at(document_id).rating });
        }
        return matched_documents;
    }
};

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
    const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))

void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
    const string& hint) {
    if (!value) {
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))

template <typename Function>
void RunTestImpl(const Function& fun, const string& fun_name) {
    fun();
    cerr << fun_name << " OK" << endl;
}

#define RUN_TEST(func) RunTestImpl((func), #func)

// -------- Начало модульных тестов поисковой системы ----------

void TestsAddedDocuments() {
    const string content0 = "белый кот и модный ошейник"s;
    const string content1 = "пушистый кот пушистый хвост"s;
    const vector<int> ratings = { 8, -3 };

    {
        SearchServer server;
        ASSERT_EQUAL(server.GetDocumentCount(), 0);
        server.AddDocument(0, content0, DocumentStatus::ACTUAL, ratings);
        ASSERT_EQUAL(server.GetDocumentCount(), 1);
        server.AddDocument(1, content1, DocumentStatus::ACTUAL, ratings);
        ASSERT_EQUAL(server.GetDocumentCount(), 2);
    }
}

void TestsAddedMatchWords() {
    int id0 = 0;
    const string content0 = "белый кот и модный ошейник"s;
    int id1 = 1;
    const string content1 = "пушистый кот пушистый хвост"s;
    const vector<int> ratings = { 8, -3 };

    {
        SearchServer server;
        server.AddDocument(id0, content0, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(id1, content1, DocumentStatus::ACTUAL, ratings);
        const auto& [words0, status0] = server.MatchDocument("пушистый кот", id0);
        const auto& [words1, status1] = server.MatchDocument("пушистый кот", id1);
        ASSERT_EQUAL(words0.size(), 1);
        ASSERT_EQUAL(words1.size(), 2);
    }
}

void TestsStopWords() {
    const string content0 = "кот и ошейник"s;
    const vector<int> ratings = { 8, -3 };

    {
        SearchServer server;
        server.SetStopWords("и"s);
        server.AddDocument(0, content0, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("и"s);
        ASSERT(found_docs.empty());
    }
}

void TestsMinusWords() {
    const string content0 = "ухоженный пёс выразительные глаза"s;
    const string content1 = "маленький пёс огромная лапа"s;
    const vector<int> ratings = { 8, -3 };

    {
        SearchServer server;
        server.AddDocument(0, content0, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(1, content1, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("пёс -лапа"s);
        ASSERT_EQUAL_HINT(found_docs.size(), 1, "incorrect number of documents");
    }
}

void TestsRatings() {
    const string content = "белый кот и модный ошейник"s;
    const vector<int> ratings0 = { 8, -3 };
    const vector<int> ratings1 = { 7, 2, 7 };
    const vector<int> ratings2 = { 5, -12, 2, 1 };

    {
        SearchServer server;

        server.AddDocument(0, content, DocumentStatus::ACTUAL, ratings0);
        server.AddDocument(1, content, DocumentStatus::ACTUAL, ratings1);
        server.AddDocument(2, content, DocumentStatus::ACTUAL, ratings2);

        const auto found_docs = server.FindTopDocuments("кот"s);

        const Document& doc1 = found_docs[0];
        ASSERT_EQUAL(doc1.rating, 5);

        const Document& doc0 = found_docs[1];
        ASSERT_EQUAL(doc0.rating, 2);

        const Document& doc2 = found_docs[2];
        ASSERT_EQUAL(doc2.rating, -1);
    }
}

void TestsRelevance() {
    const string content0 = "белый кот и модный ошейник"s;
    const string content1 = "пушистый кот пушистый хвост"s;
    const string content2 = "ухоженный пёс выразительные глаза"s;
    const vector<int> ratings = { 8, -3 };

    {
        SearchServer server;

        server.AddDocument(0, content0, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(1, content1, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(2, content2, DocumentStatus::ACTUAL, ratings);

        const auto found_docs = server.FindTopDocuments("пушистый ухоженный кот"s);

        double idf_fluffy = log(3.0 / 1.0);
        double idf_groom = log(3.0 / 1.0);
        double idf_cat = log(3.0 / 2.0);
        
        double tf_idf_0 = (1.0 / 5.0) * idf_cat;
        double tf_idf_1 = ((2.0 / 4.0) * idf_fluffy) + ((1.0 / 4) * idf_cat);
        double tf_idf_2 = (1.0 / 4.0) * idf_groom;

        const Document& doc1 = found_docs[0];
        ASSERT_EQUAL(doc1.relevance, tf_idf_1);

        const Document& doc2 = found_docs[1];
        ASSERT_EQUAL(doc2.relevance, tf_idf_2);

        const Document& doc0 = found_docs[2];
        ASSERT_EQUAL(doc0.relevance, tf_idf_0);
    }
}

void TestsSortingByRelevance() {
    int id0 = 0;
    const string content0 = "белый кот и модный ошейник"s;
    int id1 = 1;
    const string content1 = "пушистый кот пушистый хвост"s;
    int id2 = 2;
    const string content2 = "ухоженный пёс выразительные глаза"s;
    const vector<int> ratings = { 8, -3 };

    {
        SearchServer server;

        server.AddDocument(id0, content0, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(id1, content1, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(id2, content2, DocumentStatus::ACTUAL, ratings);

        const auto found_docs = server.FindTopDocuments("пушистый ухоженный кот"s);

        auto sort_found_docs = [](const Document& lhs, const Document& rhs) {
            return lhs.relevance > rhs.relevance;
        };

        ASSERT(is_sorted(begin(found_docs), end(found_docs), sort_found_docs));
    }
}

void TestsStatusesAndCustomStatus() {
    const string content0 = "белый кот и модный ошейник"s;
    const string content1 = "пушистый кот пушистый хвост"s;
    const string content2 = "ухоженный пёс выразительные глаза"s;
    const string content3 = "ухоженный скворец евгений"s;
    const vector<int> ratings = { 9 }; 
    const string query = "пушистый ухоженный кот"s;

    {
        SearchServer server;
        server.AddDocument(0, content0, DocumentStatus::ACTUAL, ratings);
        server.AddDocument(1, content1, DocumentStatus::IRRELEVANT, ratings);
        server.AddDocument(2, content2, DocumentStatus::BANNED, ratings);
        server.AddDocument(3, content3, DocumentStatus::REMOVED, ratings);

        const auto found_docs_actual = server.FindTopDocuments(
            query);
        const auto found_docs_irrelevant = server.FindTopDocuments(
            query, DocumentStatus::IRRELEVANT);
        const auto found_docs_banned = server.FindTopDocuments(
            query, DocumentStatus::BANNED);
        const auto found_docs_removed = server.FindTopDocuments(
            query, DocumentStatus::REMOVED);

        const auto found_docs_custom = server.FindTopDocuments(
            query, [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; });

        ASSERT_EQUAL_HINT(found_docs_actual.size(), 1, "incorrect number of status ACTUAL");
        ASSERT_EQUAL_HINT(found_docs_irrelevant.size(), 1, "incorrect number of status IRRELEVANT");
        ASSERT_EQUAL_HINT(found_docs_banned.size(), 1, "incorrect number of status BANNED");
        ASSERT_EQUAL_HINT(found_docs_removed.size(), 1, "incorrect number of status REMOVED");

        ASSERT_EQUAL_HINT(found_docs_custom.size(), 2, "incorrect number of custom status");
    }
}

void TestSearchServer() {
    RUN_TEST(TestsAddedDocuments);
    RUN_TEST(TestsAddedMatchWords);
    RUN_TEST(TestsStopWords);
    RUN_TEST(TestsMinusWords);
    RUN_TEST(TestsRatings);
    RUN_TEST(TestsRelevance);
    RUN_TEST(TestsSortingByRelevance);
    RUN_TEST(TestsStatusesAndCustomStatus); 
}

// --------- Окончание модульных тестов поисковой системы -----------

int main() {
    setlocale(LC_ALL, "Russian");
    TestSearchServer();
}
