#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

// ExamGrid: a C++ examination-centre and seating operations simulator.
// Core DSA structures (BST, circular queue, linked list and stacks) are implemented manually.

namespace {
constexpr int MAX_CANDIDATES = 10000;
constexpr int MAX_CENTRES = 12;
constexpr int MAX_HALLS = 8;
constexpr int MAX_ROWS = 25;
constexpr int MAX_COLS = 25;
constexpr int MAX_UNDO = 5;
constexpr const char* STATE_FILE = "examgrid_state.txt";

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string readLine(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

int readInt(const std::string& prompt, int minValue, int maxValue) {
    while (true) {
        const std::string s = readLine(prompt);
        try {
            std::size_t used = 0;
            const int value = std::stoi(s, &used);
            if (used == s.size() && value >= minValue && value <= maxValue) return value;
        } catch (...) {}
        std::cout << "Enter a number between " << minValue << " and " << maxValue << ".\n";
    }
}

bool confirm(const std::string& prompt) {
    const std::string answer = readLine(prompt + " (y/N): ");
    return answer == "y" || answer == "Y" || answer == "yes" || answer == "YES";
}

std::string csvEscape(const std::string& value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result += '"';
        result += c;
    }
    result += '"';
    return result;
}

std::array<std::string, 4> parseCsv4(const std::string& line, bool& ok) {
    std::array<std::string, 4> fields{};
    std::string value;
    bool inQuotes = false;
    int field = 0;
    ok = true;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                value += '"';
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            if (field >= 4) { ok = false; return fields; }
            fields[field++] = value;
            value.clear();
        } else {
            value += c;
        }
    }
    if (inQuotes || field != 3) { ok = false; return fields; }
    fields[3] = value;
    for (const auto& f : fields) {
        if (f.find('\n') != std::string::npos || f.find('\r') != std::string::npos) {
            ok = false;
            break;
        }
    }
    return fields;
}

std::string jsonQuote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string apiResult(bool ok, const std::string& message, const std::string& extra = "") {
    return std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":" + jsonQuote(message) +
           (extra.empty() ? "" : "," + extra) + "}";
}

struct Candidate {
    std::string id;
    std::string name;
    std::string preference1;
    std::string preference2;
    int centreIndex = -1;
    int hallIndex = -1;
    int row = -1;
    int col = -1;
    bool allocated = false;
    bool checkedIn = false;
};

struct Hall {
    int rows = 0;
    int cols = 0;
    int seats[MAX_ROWS][MAX_COLS]{}; // candidate array indices; -1 means empty

    void clear() {
        for (int r = 0; r < MAX_ROWS; ++r)
            for (int c = 0; c < MAX_COLS; ++c)
                seats[r][c] = -1;
    }
    int capacity() const { return rows * cols; }
};

struct Centre {
    std::string id;
    std::string name;
    std::string city;
    bool available = true;
    int hallCount = 0;
    Hall halls[MAX_HALLS];
};

// Manually implemented binary search tree index, ordered by application ID.
class CandidateBST {
    struct Node { int candidateIndex = -1; int left = -1; int right = -1; };
    Node nodes[MAX_CANDIDATES];
    int root = -1;
    int used = 0;

public:
    void clear() { root = -1; used = 0; }

    bool insert(int candidateIndex, const Candidate* candidates) {
        if (used >= MAX_CANDIDATES) return false;
        if (root == -1) {
            root = used;
            nodes[used++] = {candidateIndex, -1, -1};
            return true;
        }
        int current = root;
        while (true) {
            const std::string& key = candidates[candidateIndex].id;
            const std::string& currentKey = candidates[nodes[current].candidateIndex].id;
            if (key == currentKey) return false;
            if (key < currentKey) {
                if (nodes[current].left == -1) {
                    nodes[current].left = used;
                    nodes[used++] = {candidateIndex, -1, -1};
                    return true;
                }
                current = nodes[current].left;
            } else {
                if (nodes[current].right == -1) {
                    nodes[current].right = used;
                    nodes[used++] = {candidateIndex, -1, -1};
                    return true;
                }
                current = nodes[current].right;
            }
        }
    }

    int search(const std::string& id, const Candidate* candidates, int* comparisons = nullptr) const {
        int current = root;
        if (comparisons) *comparisons = 0;
        while (current != -1) {
            if (comparisons) ++(*comparisons);
            const Candidate& candidate = candidates[nodes[current].candidateIndex];
            if (id == candidate.id) return nodes[current].candidateIndex;
            current = id < candidate.id ? nodes[current].left : nodes[current].right;
        }
        return -1;
    }

    template <typename Function>
    void forEachInOrder(Function fn) const {
        int stack[MAX_CANDIDATES];
        int top = 0;
        int current = root;
        while (current != -1 || top > 0) {
            while (current != -1) {
                if (top >= MAX_CANDIDATES) return;
                stack[top++] = current;
                current = nodes[current].left;
            }
            current = stack[--top];
            fn(nodes[current].candidateIndex);
            current = nodes[current].right;
        }
    }
};

// Manually implemented circular FIFO queue for check-in.
class CheckInQueue {
    int data[MAX_CANDIDATES];
    int head = 0;
    int tail = 0;
    int count = 0;
public:
    void clear() { head = tail = count = 0; }
    bool enqueue(int value) {
        if (count == MAX_CANDIDATES) return false;
        data[tail] = value;
        tail = (tail + 1) % MAX_CANDIDATES;
        ++count;
        return true;
    }
    bool dequeue(int& value) {
        if (count == 0) return false;
        value = data[head];
        head = (head + 1) % MAX_CANDIDATES;
        --count;
        return true;
    }
    int size() const { return count; }
    int peekAt(int offset) const {
        if (offset < 0 || offset >= count) return -1;
        return data[(head + offset) % MAX_CANDIDATES];
    }
};

// Manually implemented linked list used as an append-only event/audit trail.
class AuditList {
public:
    struct Node { std::string message; Node* next = nullptr; };
private:
    Node* head = nullptr;
    Node* tail = nullptr;
    int count = 0;
public:
    ~AuditList() { clear(); }
    AuditList() = default;
    AuditList(const AuditList&) = delete;
    AuditList& operator=(const AuditList&) = delete;

    void clear() {
        Node* current = head;
        while (current) {
            Node* next = current->next;
            delete current;
            current = next;
        }
        head = tail = nullptr;
        count = 0;
    }
    void append(const std::string& message) {
        Node* node = new Node{"[" + timestamp() + "] " + message, nullptr};
        if (!head) head = tail = node;
        else { tail->next = node; tail = node; }
        ++count;
    }
    void appendExisting(const std::string& message) {
        Node* node = new Node{message, nullptr};
        if (!head) head = tail = node;
        else { tail->next = node; tail = node; }
        ++count;
    }
    int size() const { return count; }
    std::vector<std::string> recent(int lastN = 50) const {
        std::vector<std::string> entries;
        if (lastN <= 0) return entries;
        const int keep = std::min(lastN, count);
        const Node* current = head;
        while (current) {
            entries.push_back(current->message);
            if (static_cast<int>(entries.size()) > keep) entries.erase(entries.begin());
            current = current->next;
        }
        return entries;
    }
    void print(int lastN = 25) const {
        if (!head) { std::cout << "No audit events yet.\n"; return; }
        // For this bounded college simulator, keep the recent entries in a small manual stack.
        const int keep = std::min(lastN, count);
        const Node* pointers[200];
        int used = 0;
        const Node* current = head;
        while (current) {
            if (used < keep) pointers[used++] = current;
            else {
                for (int i = 1; i < keep; ++i) pointers[i - 1] = pointers[i];
                pointers[keep - 1] = current;
            }
            current = current->next;
        }
        for (int i = 0; i < used; ++i) std::cout << "  " << pointers[i]->message << '\n';
    }
    void save(std::ostream& out) const {
        out << count << '\n';
        for (const Node* node = head; node; node = node->next) out << std::quoted(node->message) << '\n';
    }
    bool load(std::istream& in) {
        clear();
        int entries = 0;
        if (!(in >> entries) || entries < 0 || entries > 100000) return false;
        in.ignore(100000, '\n');
        for (int i = 0; i < entries; ++i) {
            std::string message;
            if (!(in >> std::quoted(message))) return false;
            in.ignore(100000, '\n');
            appendExisting(message);
        }
        return true;
    }
};

struct AssignmentState {
    int centreIndex;
    int hallIndex;
    int row;
    int col;
    bool allocated;
    bool checkedIn;
};

struct UndoSnapshot {
    std::string label;
    int candidateCount = 0;
    int centreCount = 0;
    bool wasLocked = false;
    AssignmentState candidates[MAX_CANDIDATES];
    bool centreAvailable[MAX_CENTRES];
};

// Manually implemented LIFO stack for reversible allocation changes.
class UndoStack {
    UndoSnapshot* items[MAX_UNDO]{};
    int count = 0;
public:
    ~UndoStack() { clear(); }
    UndoStack() = default;
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;
    void clear() { for (int i = 0; i < count; ++i) delete items[i]; count = 0; }
    int size() const { return count; }
    void push(UndoSnapshot* snapshot) {
        if (count == MAX_UNDO) {
            delete items[0];
            for (int i = 1; i < count; ++i) items[i - 1] = items[i];
            --count;
        }
        items[count++] = snapshot;
    }
    UndoSnapshot* pop() {
        if (count == 0) return nullptr;
        return items[--count];
    }
};

class ExamGrid {
    Candidate candidates[MAX_CANDIDATES];
    int candidateCount = 0;
    Centre centres[MAX_CENTRES];
    int centreCount = 0;
    CandidateBST bst;
    CheckInQueue checkInQueue;
    int checkInHistory[MAX_CANDIDATES]; // manual stack of most recent check-ins
    int checkInHistoryCount = 0;
    UndoStack undoStack;
    AuditList audit;
    bool planLocked = false;

    void clearSeatGrids() {
        for (int c = 0; c < centreCount; ++c)
            for (int h = 0; h < centres[c].hallCount; ++h)
                centres[c].halls[h].clear();
    }

    void rebuildSeatGrids() {
        clearSeatGrids();
        for (int i = 0; i < candidateCount; ++i) {
            Candidate& cand = candidates[i];
            if (!cand.allocated || cand.centreIndex < 0 || cand.centreIndex >= centreCount) continue;
            Centre& centre = centres[cand.centreIndex];
            if (cand.hallIndex < 0 || cand.hallIndex >= centre.hallCount) continue;
            Hall& hall = centre.halls[cand.hallIndex];
            if (cand.row >= 0 && cand.row < hall.rows && cand.col >= 0 && cand.col < hall.cols)
                hall.seats[cand.row][cand.col] = i;
        }
    }

    UndoSnapshot* makeSnapshot(const std::string& label) const {
        auto* snapshot = new UndoSnapshot;
        snapshot->label = label;
        snapshot->candidateCount = candidateCount;
        snapshot->centreCount = centreCount;
        snapshot->wasLocked = planLocked;
        for (int i = 0; i < candidateCount; ++i) {
            const Candidate& c = candidates[i];
            snapshot->candidates[i] = {c.centreIndex, c.hallIndex, c.row, c.col, c.allocated, c.checkedIn};
        }
        for (int i = 0; i < centreCount; ++i) snapshot->centreAvailable[i] = centres[i].available;
        return snapshot;
    }

    void restoreSnapshot(UndoSnapshot* snapshot) {
        if (!snapshot) return;
        candidateCount = std::min(snapshot->candidateCount, MAX_CANDIDATES);
        const int sharedCentres = std::min(centreCount, snapshot->centreCount);
        for (int i = 0; i < candidateCount; ++i) {
            Candidate& c = candidates[i];
            const AssignmentState& s = snapshot->candidates[i];
            c.centreIndex = s.centreIndex;
            c.hallIndex = s.hallIndex;
            c.row = s.row;
            c.col = s.col;
            c.allocated = s.allocated;
            c.checkedIn = s.checkedIn;
        }
        for (int i = 0; i < sharedCentres; ++i) centres[i].available = snapshot->centreAvailable[i];
        planLocked = snapshot->wasLocked;
        bst.clear();
        for (int i = 0; i < candidateCount; ++i) bst.insert(i, candidates);
        rebuildSeatGrids();
        checkInQueue.clear();
        checkInHistoryCount = 0;
        audit.append("Undo applied: " + snapshot->label + ". Seating and allocation state restored.");
        delete snapshot;
    }

    void addCentreInternal(const std::string& id, const std::string& name, const std::string& city,
                           int halls, int rows, int cols) {
        if (centreCount >= MAX_CENTRES) return;
        Centre& centre = centres[centreCount++];
        centre.id = id;
        centre.name = name;
        centre.city = city;
        centre.available = true;
        centre.hallCount = halls;
        for (int i = 0; i < halls; ++i) {
            centre.halls[i].rows = rows;
            centre.halls[i].cols = cols;
            centre.halls[i].clear();
        }
    }

    void configureDefaultCentres() {
        centreCount = 0;
        addCentreInternal("C001", "Indore Examination Hub", "Indore", 2, 10, 15);
        addCentreInternal("C002", "Nashik Examination Hub", "Nashik", 2, 10, 15);
        addCentreInternal("C003", "Pune Examination Hub", "Pune", 2, 10, 15);
    }

    int findCentreById(const std::string& id) const {
        for (int i = 0; i < centreCount; ++i) if (centres[i].id == id) return i;
        return -1;
    }

    int totalCapacity(bool availableOnly = true) const {
        int total = 0;
        for (int i = 0; i < centreCount; ++i) {
            if (availableOnly && !centres[i].available) continue;
            for (int h = 0; h < centres[i].hallCount; ++h) total += centres[i].halls[h].capacity();
        }
        return total;
    }

    int occupiedSeatCount() const {
        int total = 0;
        for (int i = 0; i < candidateCount; ++i) if (candidates[i].allocated) ++total;
        return total;
    }

    int checkedInCount() const {
        int total = 0;
        for (int i = 0; i < candidateCount; ++i) if (candidates[i].checkedIn) ++total;
        return total;
    }

    bool hasAnyCheckIns() const { return checkedInCount() > 0; }

    bool allocateCandidateAt(int candidateIndex, int centreIndex) {
        if (centreIndex < 0 || centreIndex >= centreCount || !centres[centreIndex].available) return false;
        Centre& centre = centres[centreIndex];
        for (int h = 0; h < centre.hallCount; ++h) {
            Hall& hall = centre.halls[h];
            for (int r = 0; r < hall.rows; ++r) {
                for (int c = 0; c < hall.cols; ++c) {
                    if (hall.seats[r][c] == -1) {
                        hall.seats[r][c] = candidateIndex;
                        Candidate& cand = candidates[candidateIndex];
                        cand.centreIndex = centreIndex;
                        cand.hallIndex = h;
                        cand.row = r;
                        cand.col = c;
                        cand.allocated = true;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool allocateCandidate(int candidateIndex) {
        Candidate& candidate = candidates[candidateIndex];
        candidate.allocated = false;
        candidate.centreIndex = candidate.hallIndex = candidate.row = candidate.col = -1;
        // Try stated preferences first, then any remaining available centre.
        for (int c = 0; c < centreCount; ++c) {
            if (centres[c].city == candidate.preference1 && allocateCandidateAt(candidateIndex, c)) return true;
        }
        if (!candidate.preference2.empty()) {
            for (int c = 0; c < centreCount; ++c) {
                if (centres[c].city == candidate.preference2 && allocateCandidateAt(candidateIndex, c)) return true;
            }
        }
        for (int c = 0; c < centreCount; ++c) {
            if (centres[c].city != candidate.preference1 && centres[c].city != candidate.preference2 &&
                allocateCandidateAt(candidateIndex, c)) return true;
        }
        // If a preferred centre ran out of capacity, retry any centre, including the preferences.
        for (int c = 0; c < centreCount; ++c) if (allocateCandidateAt(candidateIndex, c)) return true;
        return false;
    }

    bool addCandidate(const std::string& id, const std::string& name,
                      const std::string& pref1, const std::string& pref2, bool logEvent = true) {
        if (candidateCount >= MAX_CANDIDATES || id.empty() || name.empty()) return false;
        if (bst.search(id, candidates) != -1) return false;
        // Adding a record changes the roster, so previous assignment snapshots are no longer applicable.
        if (logEvent) undoStack.clear();
        Candidate& cand = candidates[candidateCount];
        cand = Candidate{};
        cand.id = id;
        cand.name = name;
        cand.preference1 = pref1;
        cand.preference2 = pref2;
        const int index = candidateCount;
        if (!bst.insert(index, candidates)) return false;
        ++candidateCount;
        if (logEvent) audit.append("Candidate registered: " + id + " (" + name + ").");
        return true;
    }

    void resetApplication(bool withDefaults) {
        candidateCount = 0;
        bst.clear();
        checkInQueue.clear();
        checkInHistoryCount = 0;
        undoStack.clear();
        audit.clear();
        planLocked = false;
        if (withDefaults) configureDefaultCentres();
        else centreCount = 0;
        clearSeatGrids();
    }

    void printCandidateRow(int index) const {
        const Candidate& c = candidates[index];
        std::cout << std::left << std::setw(11) << c.id
                  << std::setw(23) << c.name.substr(0, 22)
                  << std::setw(11) << c.preference1.substr(0, 10)
                  << std::setw(11) << c.preference2.substr(0, 10)
                  << std::setw(10) << (c.allocated ? centres[c.centreIndex].city : "--")
                  << (c.allocated ? (c.checkedIn ? "Checked-in" : "Allocated") : "Unallocated") << '\n';
    }

    void printCandidateHeader() const {
        std::cout << std::left << std::setw(11) << "APP ID" << std::setw(23) << "CANDIDATE"
                  << std::setw(11) << "PREF 1" << std::setw(11) << "PREF 2"
                  << std::setw(10) << "CENTRE" << "STATUS\n";
        std::cout << std::string(78, '-') << '\n';
    }

    std::string candidateJson(int index) const {
        const Candidate& c = candidates[index];
        std::ostringstream out;
        out << "{\"id\":" << jsonQuote(c.id)
            << ",\"name\":" << jsonQuote(c.name)
            << ",\"preference1\":" << jsonQuote(c.preference1)
            << ",\"preference2\":" << jsonQuote(c.preference2)
            << ",\"allocated\":" << (c.allocated ? "true" : "false")
            << ",\"checkedIn\":" << (c.checkedIn ? "true" : "false")
            << ",\"centreId\":" << jsonQuote(c.allocated ? centres[c.centreIndex].id : "")
            << ",\"centreName\":" << jsonQuote(c.allocated ? centres[c.centreIndex].name : "")
            << ",\"city\":" << jsonQuote(c.allocated ? centres[c.centreIndex].city : "")
            << ",\"hall\":" << (c.allocated ? c.hallIndex + 1 : 0)
            << ",\"row\":" << (c.allocated ? c.row + 1 : 0)
            << ",\"seat\":" << (c.allocated ? c.col + 1 : 0) << "}";
        return out.str();
    }

public:
    ExamGrid() { configureDefaultCentres(); }
    ExamGrid(const ExamGrid&) = delete;
    ExamGrid& operator=(const ExamGrid&) = delete;

    bool loadState(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        std::string magic;
        int newCentreCount = 0, newCandidateCount = 0, locked = 0;
        if (!(in >> magic) || magic != "EXAMGRID_STATE_V1") return false;
        if (!(in >> newCentreCount >> newCandidateCount >> locked)) return false;
        if (newCentreCount < 0 || newCentreCount > MAX_CENTRES || newCandidateCount < 0 || newCandidateCount > MAX_CANDIDATES) return false;

        resetApplication(false);
        centreCount = newCentreCount;
        planLocked = (locked != 0);
        for (int i = 0; i < centreCount; ++i) {
            Centre& c = centres[i];
            int available = 1;
            if (!(in >> std::quoted(c.id) >> std::quoted(c.name) >> std::quoted(c.city) >> available >> c.hallCount)) return false;
            if (c.hallCount < 0 || c.hallCount > MAX_HALLS) return false;
            c.available = (available != 0);
            for (int h = 0; h < c.hallCount; ++h) {
                Hall& hall = c.halls[h];
                if (!(in >> hall.rows >> hall.cols)) return false;
                if (hall.rows < 1 || hall.rows > MAX_ROWS || hall.cols < 1 || hall.cols > MAX_COLS) return false;
                hall.clear();
            }
        }
        candidateCount = newCandidateCount;
        bst.clear();
        for (int i = 0; i < candidateCount; ++i) {
            Candidate& c = candidates[i];
            int allocated = 0, checkedIn = 0;
            if (!(in >> std::quoted(c.id) >> std::quoted(c.name) >> std::quoted(c.preference1) >> std::quoted(c.preference2)
                  >> c.centreIndex >> c.hallIndex >> c.row >> c.col >> allocated >> checkedIn)) return false;
            c.allocated = allocated != 0;
            c.checkedIn = checkedIn != 0;
            if (!bst.insert(i, candidates)) return false;
        }
        if (!audit.load(in)) return false;
        rebuildSeatGrids();
        checkInQueue.clear();
        checkInHistoryCount = 0;
        undoStack.clear();
        return true;
    }

    bool saveState(const std::string& path) const {
        std::ofstream out(path);
        if (!out) return false;
        out << "EXAMGRID_STATE_V1\n" << centreCount << ' ' << candidateCount << ' ' << (planLocked ? 1 : 0) << '\n';
        for (int i = 0; i < centreCount; ++i) {
            const Centre& c = centres[i];
            out << std::quoted(c.id) << ' ' << std::quoted(c.name) << ' ' << std::quoted(c.city) << ' '
                << (c.available ? 1 : 0) << ' ' << c.hallCount << '\n';
            for (int h = 0; h < c.hallCount; ++h) out << c.halls[h].rows << ' ' << c.halls[h].cols << '\n';
        }
        for (int i = 0; i < candidateCount; ++i) {
            const Candidate& c = candidates[i];
            out << std::quoted(c.id) << ' ' << std::quoted(c.name) << ' ' << std::quoted(c.preference1) << ' '
                << std::quoted(c.preference2) << ' ' << c.centreIndex << ' ' << c.hallIndex << ' '
                << c.row << ' ' << c.col << ' ' << (c.allocated ? 1 : 0) << ' ' << (c.checkedIn ? 1 : 0) << '\n';
        }
        audit.save(out);
        return static_cast<bool>(out);
    }

    void generateDemoData() {
        resetApplication(true);
        const std::string first[] = {"Aarav", "Aditi", "Anaya", "Arjun", "Diya", "Ishaan", "Kavya", "Mira", "Neel", "Riya",
                                     "Samar", "Sara", "Vihaan", "Zoya", "Advait", "Ira", "Kabir", "Meera", "Reyansh", "Tara"};
        const std::string last[] = {"Shah", "Patil", "Joshi", "Mehta", "Kulkarni", "Rao", "Desai", "Nair", "Khan", "Kapoor",
                                    "Iyer", "Gupta", "Pawar", "More", "Saxena", "Jain", "Sinha", "Bose", "Bhat", "Verma",
                                    "Singh", "Chopra", "Malik", "Das", "Menon", "Kale", "Chavan", "Pillai", "Ghosh", "Dube"};
        const std::string cities[] = {"Indore", "Nashik", "Pune"};
        constexpr int demoCount = 600;
        for (int i = 0; i < demoCount; ++i) {
            // A permutation of 1..600 makes the BST insertion order less predictably sorted.
            const int serial = ((i * 197) % demoCount) + 1;
            std::ostringstream id;
            id << "EG" << std::setw(6) << std::setfill('0') << serial;
            const std::string name = first[i % 20] + " " + last[(i * 7 + 3) % 30];
            const std::string pref1 = cities[i / 200];
            const std::string pref2 = cities[(i / 200 + 1) % 3];
            addCandidate(id.str(), name, pref1, pref2, false);
        }
        audit.append("Demo dataset created: 600 synthetic candidates across 3 centres.");
        audit.append("Synthetic records only; no real candidate information is used.");
        std::cout << "\nDemo dataset ready: 600 synthetic candidates, 3 centres, 6 halls.\n";
    }

    // ---------- Browser API: all business logic remains in C++ ----------
    std::string apiDashboard() const {
        const int allocated = occupiedSeatCount();
        const int capacity = totalCapacity(true);
        int activeCentres = 0;
        for (int i = 0; i < centreCount; ++i) if (centres[i].available) ++activeCentres;
        std::ostringstream out;
        out << "{\"stats\":{"
            << "\"totalCandidates\":" << candidateCount
            << ",\"allocated\":" << allocated
            << ",\"unallocated\":" << candidateCount - allocated
            << ",\"capacity\":" << capacity
            << ",\"freeSeats\":" << std::max(0, capacity - allocated)
            << ",\"utilisation\":" << (capacity ? (100.0 * allocated / capacity) : 0.0)
            << ",\"checkedIn\":" << checkedInCount()
            << ",\"queueSize\":" << checkInQueue.size()
            << ",\"activeCentres\":" << activeCentres
            << ",\"totalCentres\":" << centreCount
            << ",\"locked\":" << (planLocked ? "true" : "false")
            << ",\"undoAvailable\":" << undoStack.size()
            << ",\"auditEvents\":" << audit.size()
            << "},\"centres\":";
        out << apiCentresArray() << "}";
        return out.str();
    }

    std::string apiCentresArray() const {
        std::ostringstream out;
        out << "[";
        for (int i = 0; i < centreCount; ++i) {
            if (i) out << ",";
            const Centre& c = centres[i];
            int capacity = 0, occupied = 0;
            out << "{\"id\":" << jsonQuote(c.id) << ",\"name\":" << jsonQuote(c.name)
                << ",\"city\":" << jsonQuote(c.city) << ",\"available\":" << (c.available ? "true" : "false")
                << ",\"hallCount\":" << c.hallCount << ",\"halls\":[";
            for (int h = 0; h < c.hallCount; ++h) {
                if (h) out << ",";
                const Hall& hall = c.halls[h];
                int hallOccupied = 0;
                for (int r = 0; r < hall.rows; ++r) for (int col = 0; col < hall.cols; ++col) if (hall.seats[r][col] != -1) ++hallOccupied;
                capacity += hall.capacity(); occupied += hallOccupied;
                out << "{\"number\":" << h + 1 << ",\"rows\":" << hall.rows << ",\"cols\":" << hall.cols
                    << ",\"capacity\":" << hall.capacity() << ",\"occupied\":" << hallOccupied << "}";
            }
            out << "],\"capacity\":" << capacity << ",\"occupied\":" << occupied
                << ",\"utilisation\":" << (capacity ? (100.0 * occupied / capacity) : 0.0) << "}";
        }
        out << "]";
        return out.str();
    }

    std::string apiCandidates(const std::string& query, int limit = 100) const {
        const std::string q = lowerCopy(query);
        std::vector<int> matches;
        int total = 0;
        bst.forEachInOrder([&](int index) {
            const Candidate& c = candidates[index];
            const std::string hay = lowerCopy(c.id + " " + c.name + " " + c.preference1 + " " + c.preference2 + " " + (c.allocated ? centres[c.centreIndex].city : ""));
            if (q.empty() || hay.find(q) != std::string::npos) {
                ++total;
                if (static_cast<int>(matches.size()) < std::max(1, limit)) matches.push_back(index);
            }
        });
        std::ostringstream out;
        out << "{\"total\":" << total << ",\"shown\":" << matches.size() << ",\"candidates\":[";
        for (std::size_t i = 0; i < matches.size(); ++i) { if (i) out << ","; out << candidateJson(matches[i]); }
        out << "]}";
        return out.str();
    }

    std::string apiSearchCandidate(const std::string& id) const {
        int comparisons = 0;
        const int index = bst.search(id, candidates, &comparisons);
        if (index < 0) return apiResult(false, "No candidate matched that application ID.", "\"comparisons\":" + std::to_string(comparisons));
        return std::string("{\"ok\":true,\"message\":\"Candidate found through the Binary Search Tree.\",\"comparisons\":") +
               std::to_string(comparisons) + ",\"candidate\":" + candidateJson(index) + "}";
    }

    std::string apiSeatMap(const std::string& centreId, int hallNumber) const {
        const int ci = findCentreById(centreId);
        if (ci < 0) return apiResult(false, "Unknown examination centre.");
        const Centre& centre = centres[ci];
        if (hallNumber < 1 || hallNumber > centre.hallCount) return apiResult(false, "Invalid hall number.");
        const Hall& hall = centre.halls[hallNumber - 1];
        std::ostringstream out;
        out << "{\"centre\":" << jsonQuote(centre.name) << ",\"centreId\":" << jsonQuote(centre.id)
            << ",\"city\":" << jsonQuote(centre.city) << ",\"available\":" << (centre.available ? "true" : "false")
            << ",\"hall\":" << hallNumber << ",\"rows\":" << hall.rows << ",\"cols\":" << hall.cols << ",\"seats\":[";
        for (int r = 0; r < hall.rows; ++r) {
            if (r) out << ",";
            out << "[";
            for (int col = 0; col < hall.cols; ++col) {
                if (col) out << ",";
                const int index = hall.seats[r][col];
                if (index < 0) out << "null";
                else out << candidateJson(index);
            }
            out << "]";
        }
        out << "]}";
        return out.str();
    }

    std::string apiAudit() const {
        const auto events = audit.recent(100);
        std::ostringstream out;
        out << "{\"total\":" << audit.size() << ",\"events\":[";
        for (std::size_t i = 0; i < events.size(); ++i) { if (i) out << ","; out << jsonQuote(events[i]); }
        out << "]}";
        return out.str();
    }

    std::string apiQueue() const {
        std::ostringstream out;
        out << "{\"size\":" << checkInQueue.size() << ",\"next\":[";
        const int n = std::min(8, checkInQueue.size());
        for (int i = 0; i < n; ++i) {
            if (i) out << ",";
            const int index = checkInQueue.peekAt(i);
            if (index >= 0 && index < candidateCount) out << candidateJson(index); else out << "null";
        }
        out << "]}";
        return out.str();
    }

    std::string apiAddCandidate(const std::string& id, const std::string& name, const std::string& pref1, const std::string& pref2) {
        if (planLocked) return apiResult(false, "Unlock the seating plan before registering candidates.");
        if (id.empty() || name.empty()) return apiResult(false, "Application ID and candidate name are required.");
        if (!addCandidate(id, name, pref1, pref2)) return apiResult(false, "Candidate could not be registered. Check duplicate IDs and capacity limits.");
        audit.append("Candidate registered from web interface: " + id + " (" + name + ").");
        return apiResult(true, "Candidate registered. Run allocation to assign a seat.", "\"dsa\":\"BST insertion\"");
    }

    std::string apiAddCentre(const std::string& id, const std::string& name, const std::string& city, int halls, int rows, int cols) {
        if (planLocked) return apiResult(false, "Unlock the seating plan before changing centres.");
        if (centreCount >= MAX_CENTRES) return apiResult(false, "The configured centre limit has been reached.");
        if (id.empty() || name.empty() || city.empty()) return apiResult(false, "Centre ID, name and city are required.");
        if (findCentreById(id) >= 0) return apiResult(false, "That centre ID already exists.");
        if (halls < 1 || halls > MAX_HALLS || rows < 1 || rows > MAX_ROWS || cols < 1 || cols > MAX_COLS) return apiResult(false, "Hall, row or column settings are out of range.");
        undoStack.clear();
        addCentreInternal(id, name, city, halls, rows, cols);
        audit.append("Centre added from web interface: " + id + " / " + name + " in " + city + ".");
        return apiResult(true, "Centre added with capacity " + std::to_string(halls * rows * cols) + " seats.", "\"capacity\":" + std::to_string(halls * rows * cols));
    }

    std::string apiImportCsv(const std::string& csv) {
        if (planLocked) return apiResult(false, "Unlock the seating plan before importing candidates.");
        std::istringstream input(csv);
        std::string line;
        int added = 0, skipped = 0, lineNumber = 0;
        undoStack.clear();
        while (std::getline(input, line)) {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (lineNumber == 1 && line.find("candidate_id") != std::string::npos) continue;
            bool ok = false;
            auto fields = parseCsv4(line, ok);
            if (!ok || fields[0].empty() || fields[1].empty()) { ++skipped; continue; }
            if (addCandidate(fields[0], fields[1], fields[2], fields[3], false)) ++added;
            else ++skipped;
        }
        audit.append("Web CSV import: " + std::to_string(added) + " added, " + std::to_string(skipped) + " skipped.");
        return apiResult(true, "CSV import complete.", "\"added\":" + std::to_string(added) + ",\"skipped\":" + std::to_string(skipped));
    }

    std::string apiAllocate() {
        if (planLocked) return apiResult(false, "The plan is locked. Unlock before changing allocations.");
        if (candidateCount == 0) return apiResult(false, "There are no candidates to allocate.");
        if (hasAnyCheckIns()) return apiResult(false, "Allocation is disabled after check-in begins. Undo check-ins first.");
        if (totalCapacity(true) == 0) return apiResult(false, "No available seating capacity. Add or reactivate a centre.");
        undoStack.push(makeSnapshot("seat allocation"));
        for (int i = 0; i < candidateCount; ++i) {
            candidates[i].allocated = false;
            candidates[i].centreIndex = candidates[i].hallIndex = candidates[i].row = candidates[i].col = -1;
        }
        clearSeatGrids();
        int allocated = 0;
        for (int i = 0; i < candidateCount; ++i) if (allocateCandidate(i)) ++allocated;
        checkInQueue.clear();
        audit.append("Web allocation completed: " + std::to_string(allocated) + " of " + std::to_string(candidateCount) + " candidates assigned.");
        return apiResult(true, "Allocation completed.", "\"allocated\":" + std::to_string(allocated) + ",\"unallocated\":" + std::to_string(candidateCount - allocated));
    }

    std::string apiPrepareQueue() {
        if (occupiedSeatCount() == 0) return apiResult(false, "Allocate seats before preparing the check-in queue.");
        checkInQueue.clear();
        int queued = 0;
        for (int i = 0; i < candidateCount; ++i) if (candidates[i].allocated && !candidates[i].checkedIn && checkInQueue.enqueue(i)) ++queued;
        audit.append("Web check-in queue prepared with " + std::to_string(queued) + " candidates.");
        return apiResult(true, "FIFO check-in queue prepared.", "\"queued\":" + std::to_string(queued));
    }

    std::string apiProcessCheckIn() {
        int index = -1;
        if (!checkInQueue.dequeue(index)) return apiResult(false, "The check-in queue is empty. Prepare it first.");
        if (index < 0 || index >= candidateCount || candidates[index].checkedIn || !candidates[index].allocated) return apiResult(false, "This queue entry is no longer eligible. Rebuild the queue.");
        candidates[index].checkedIn = true;
        if (checkInHistoryCount < MAX_CANDIDATES) checkInHistory[checkInHistoryCount++] = index;
        const Candidate& c = candidates[index];
        audit.append("Web check-in: " + c.id + " (" + c.name + ").");
        return apiResult(true, "Candidate checked in: " + c.name + ".", "\"candidate\":" + candidateJson(index));
    }

    std::string apiUndoCheckIn() {
        if (checkInHistoryCount == 0) return apiResult(false, "There is no check-in action to undo in this session.");
        const int index = checkInHistory[--checkInHistoryCount];
        if (index < 0 || index >= candidateCount || !candidates[index].checkedIn) return apiResult(false, "The latest check-in cannot be reversed.");
        candidates[index].checkedIn = false;
        checkInQueue.clear();
        audit.append("Web check-in reversed for " + candidates[index].id + "; queue cleared and must be prepared again.");
        return apiResult(true, "Last check-in reversed. Prepare the FIFO queue again before processing more candidates.", "\"candidateId\":" + jsonQuote(candidates[index].id));
    }

    std::string apiSimulateCentreFailure(const std::string& centreId) {
        if (planLocked) return apiResult(false, "Unlock the plan before simulating a centre failure.");
        if (hasAnyCheckIns()) return apiResult(false, "Centre failure simulation is only available before check-in begins.");
        const int failed = findCentreById(centreId);
        if (failed < 0) return apiResult(false, "Unknown examination centre.");
        if (!centres[failed].available) return apiResult(false, "That centre is already offline.");
        undoStack.push(makeSnapshot("centre failure at " + centres[failed].id));
        centres[failed].available = false;
        int displaced = 0;
        for (int i = 0; i < candidateCount; ++i) {
            Candidate& c = candidates[i];
            if (c.allocated && c.centreIndex == failed) {
                c.allocated = false; c.centreIndex = c.hallIndex = c.row = c.col = -1; ++displaced;
            }
        }
        rebuildSeatGrids();
        int reallocated = 0;
        UndoSnapshot* snapshot = undoStack.pop();
        if (snapshot) {
            for (int i = 0; i < std::min(candidateCount, snapshot->candidateCount); ++i) {
                if (snapshot->candidates[i].allocated && snapshot->candidates[i].centreIndex == failed) if (allocateCandidate(i)) ++reallocated;
            }
            undoStack.push(snapshot);
        }
        checkInQueue.clear();
        const int stranded = displaced - reallocated;
        audit.append("Web centre failure at " + centres[failed].id + ": " + std::to_string(displaced) + " displaced, " + std::to_string(reallocated) + " reallocated, " + std::to_string(stranded) + " still unallocated.");
        return apiResult(true, "Centre disruption simulation completed.", "\"displaced\":" + std::to_string(displaced) + ",\"reallocated\":" + std::to_string(reallocated) + ",\"stranded\":" + std::to_string(stranded));
    }

    std::string apiUndoAllocation() {
        if (planLocked) return apiResult(false, "The plan is locked. Unlock it before undoing allocation changes.");
        UndoSnapshot* snapshot = undoStack.pop();
        if (!snapshot) return apiResult(false, "There is no allocation change available to undo.");
        const std::string label = snapshot->label;
        restoreSnapshot(snapshot);
        return apiResult(true, "Undid: " + label + ". Allocation state restored.");
    }

    std::string apiSetLock(bool targetLocked, bool allowPartial) {
        if (targetLocked == planLocked) return apiResult(true, targetLocked ? "The plan is already locked." : "The plan is already unlocked.");
        if (targetLocked) {
            if (candidateCount > 0 && occupiedSeatCount() == 0) return apiResult(false, "Allocate candidates before locking the seating plan.");
            if (candidateCount > occupiedSeatCount() && !allowPartial) return apiResult(false, "Some candidates are unallocated. Confirm partial lock to proceed.", "\"requiresConfirmation\":true");
            planLocked = true;
            audit.append("Seating plan LOCKED from web interface.");
            return apiResult(true, "Seating plan locked. Allocation changes are disabled.");
        }
        planLocked = false;
        audit.append("Seating plan UNLOCKED from web interface for authorised changes.");
        return apiResult(true, "Seating plan unlocked for authorised changes.");
    }

    std::string apiResetDemo() {
        generateDemoData();
        return apiResult(true, "Demo data regenerated: 600 synthetic candidates and three centres.");
    }

    std::string apiExportCandidates() const {
        std::ostringstream out;
        out << "candidate_id,name,preference1,preference2,assigned_centre,hall,row,seat,allocated,checked_in\n";
        for (int i = 0; i < candidateCount; ++i) {
            const Candidate& c = candidates[i];
            out << csvEscape(c.id) << ',' << csvEscape(c.name) << ',' << csvEscape(c.preference1) << ',' << csvEscape(c.preference2) << ','
                << csvEscape(c.allocated ? centres[c.centreIndex].id : "") << ',' << (c.allocated ? c.hallIndex + 1 : 0) << ','
                << (c.allocated ? c.row + 1 : 0) << ',' << (c.allocated ? c.col + 1 : 0) << ','
                << (c.allocated ? "yes" : "no") << ',' << (c.checkedIn ? "yes" : "no") << '\n';
        }
        return out.str();
    }

    std::string apiExportSeats() const {
        std::ostringstream out;
        out << "centre_id,centre_name,city,hall,row,seat,candidate_id,candidate_name\n";
        for (int c = 0; c < centreCount; ++c) for (int h = 0; h < centres[c].hallCount; ++h) {
            const Hall& hall = centres[c].halls[h];
            for (int r = 0; r < hall.rows; ++r) for (int col = 0; col < hall.cols; ++col) {
                const int index = hall.seats[r][col]; if (index == -1) continue;
                out << csvEscape(centres[c].id) << ',' << csvEscape(centres[c].name) << ',' << csvEscape(centres[c].city) << ','
                    << h + 1 << ',' << r + 1 << ',' << col + 1 << ',' << csvEscape(candidates[index].id) << ',' << csvEscape(candidates[index].name) << '\n';
            }
        }
        return out.str();
    }

    void showDashboard() const {
        int allocated = occupiedSeatCount();
        int capacity = totalCapacity(true);
        int checked = checkedInCount();
        int unavailable = 0;
        for (int c = 0; c < centreCount; ++c) if (!centres[c].available) ++unavailable;
        std::cout << "\nEXAMGRID / OPERATIONS OVERVIEW\n" << std::string(45, '=') << '\n';
        std::cout << "Candidates registered : " << candidateCount << '\n';
        std::cout << "Seats allocated       : " << allocated << '\n';
        std::cout << "Candidates unallocated: " << (candidateCount - allocated) << '\n';
        std::cout << "Available capacity    : " << capacity << '\n';
        std::cout << "Capacity utilisation  : " << (capacity ? (100.0 * allocated / capacity) : 0.0) << "%\n";
        std::cout << "Checked in            : " << checked << '\n';
        std::cout << "Waiting check-in queue: " << checkInQueue.size() << '\n';
        std::cout << "Centres active        : " << (centreCount - unavailable) << " / " << centreCount << '\n';
        std::cout << "Seating plan          : " << (planLocked ? "LOCKED" : "UNLOCKED") << '\n';
        std::cout << "Undoable plan changes : " << undoStack.size() << '\n';
        std::cout << "Audit events          : " << audit.size() << '\n';
    }

    void listCentres() const {
        if (centreCount == 0) { std::cout << "No centres configured.\n"; return; }
        std::cout << "\nCENTRES & CAPACITY\n" << std::string(76, '-') << '\n';
        std::cout << std::left << std::setw(8) << "ID" << std::setw(27) << "CENTRE" << std::setw(12) << "CITY"
                  << std::setw(9) << "STATUS" << std::setw(10) << "HALLS" << "CAPACITY / OCCUPIED\n";
        for (int i = 0; i < centreCount; ++i) {
            const Centre& c = centres[i];
            int cap = 0, occupied = 0;
            for (int h = 0; h < c.hallCount; ++h) {
                cap += c.halls[h].capacity();
                for (int r = 0; r < c.halls[h].rows; ++r)
                    for (int col = 0; col < c.halls[h].cols; ++col)
                        if (c.halls[h].seats[r][col] != -1) ++occupied;
            }
            std::cout << std::left << std::setw(8) << c.id << std::setw(27) << c.name.substr(0, 26)
                      << std::setw(12) << c.city << std::setw(9) << (c.available ? "Active" : "Offline")
                      << std::setw(10) << c.hallCount << cap << " / " << occupied << '\n';
        }
    }

    void addCentre() {
        if (planLocked) { std::cout << "Unlock the seating plan before changing centres.\n"; return; }
        if (centreCount >= MAX_CENTRES) { std::cout << "Centre limit reached.\n"; return; }
        const std::string id = readLine("New centre ID (e.g. C004): ");
        if (id.empty() || findCentreById(id) != -1) { std::cout << "ID is empty or already exists.\n"; return; }
        const std::string name = readLine("Centre name: ");
        const std::string city = readLine("City (must match candidate preferences): ");
        if (name.empty() || city.empty()) { std::cout << "Name and city are required.\n"; return; }
        const int halls = readInt("Number of halls (1-8): ", 1, MAX_HALLS);
        const int rows = readInt("Rows per hall (1-25): ", 1, MAX_ROWS);
        const int cols = readInt("Columns per hall (1-25): ", 1, MAX_COLS);
        undoStack.clear(); // Centre capacity changes invalidate prior allocation snapshots.
        addCentreInternal(id, name, city, halls, rows, cols);
        audit.append("Centre added: " + id + " / " + name + " in " + city + ".");
        std::cout << "Centre added with capacity " << halls * rows * cols << ".\n";
    }

    void addCandidateInteractive() {
        if (planLocked) { std::cout << "Unlock the seating plan before registering more candidates.\n"; return; }
        if (candidateCount >= MAX_CANDIDATES) { std::cout << "Candidate limit reached.\n"; return; }
        const std::string id = readLine("Application ID: ");
        const std::string name = readLine("Candidate name: ");
        const std::string pref1 = readLine("Preferred city 1 (Indore/Nashik/Pune or configured city): ");
        const std::string pref2 = readLine("Preferred city 2 (or leave blank): ");
        if (addCandidate(id, name, pref1, pref2)) std::cout << "Candidate registered. Allocate seats to assign a hall and seat.\n";
        else std::cout << "Could not add candidate. Check the ID is unique and required fields are valid.\n";
    }

    void importCandidatesCsv() {
        if (planLocked) { std::cout << "Unlock the seating plan before importing candidates.\n"; return; }
        const std::string path = readLine("CSV path [candidates.csv]: ");
        const std::string actualPath = path.empty() ? "candidates.csv" : path;
        std::ifstream in(actualPath);
        if (!in) { std::cout << "Could not open " << actualPath << ".\n"; return; }
        undoStack.clear(); // Imported records change the candidate roster.
        std::string line;
        int added = 0, skipped = 0, lineNumber = 0;
        while (std::getline(in, line)) {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (lineNumber == 1 && line.find("candidate_id") != std::string::npos) continue;
            bool ok = false;
            auto fields = parseCsv4(line, ok);
            if (!ok || fields[0].empty() || fields[1].empty()) { ++skipped; continue; }
            if (addCandidate(fields[0], fields[1], fields[2], fields[3], false)) ++added;
            else ++skipped;
        }
        audit.append("CSV import from " + actualPath + ": " + std::to_string(added) + " added, " + std::to_string(skipped) + " skipped.");
        std::cout << "Import complete: " << added << " added, " << skipped << " skipped.\n";
    }

    void searchCandidate() const {
        const std::string id = readLine("Application ID to search: ");
        const int index = bst.search(id, candidates);
        if (index == -1) { std::cout << "Candidate not found.\n"; return; }
        const Candidate& c = candidates[index];
        std::cout << "\nCANDIDATE RECORD\n" << std::string(35, '-') << '\n';
        std::cout << "Application ID : " << c.id << '\n';
        std::cout << "Name           : " << c.name << '\n';
        std::cout << "Preferences    : " << c.preference1 << (c.preference2.empty() ? "" : " -> " + c.preference2) << '\n';
        if (c.allocated) {
            const Centre& centre = centres[c.centreIndex];
            std::cout << "Allocation     : " << centre.name << " / Hall " << (c.hallIndex + 1)
                      << " / Row " << (c.row + 1) << " / Seat " << (c.col + 1) << '\n';
        } else std::cout << "Allocation     : Not allocated\n";
        std::cout << "Attendance     : " << (c.checkedIn ? "Checked in" : "Not checked in") << '\n';
    }

    void listCandidates() const {
        if (candidateCount == 0) { std::cout << "No candidate records. Generate demo data or import a CSV.\n"; return; }
        const int limit = readInt("How many records to display (1-200, sorted by ID): ", 1, 200);
        printCandidateHeader();
        int shown = 0;
        bst.forEachInOrder([&](int idx) {
            if (shown < limit) { printCandidateRow(idx); ++shown; }
        });
        std::cout << "Displayed " << shown << " of " << candidateCount << " records in BST inorder order.\n";
    }

    void allocateAll() {
        if (planLocked) { std::cout << "Plan is locked. Unlock it before reallocating.\n"; return; }
        if (candidateCount == 0) { std::cout << "No candidates to allocate.\n"; return; }
        if (hasAnyCheckIns()) { std::cout << "Allocation is disabled after check-in has begun. Undo check-ins or restore a saved state first.\n"; return; }
        if (totalCapacity(true) == 0) { std::cout << "No available capacity. Configure an active centre first.\n"; return; }
        undoStack.push(makeSnapshot("seat allocation"));
        rebuildSeatGrids();
        for (int i = 0; i < candidateCount; ++i) {
            candidates[i].allocated = false;
            candidates[i].centreIndex = candidates[i].hallIndex = candidates[i].row = candidates[i].col = -1;
        }
        clearSeatGrids();
        int allocated = 0;
        for (int i = 0; i < candidateCount; ++i) if (allocateCandidate(i)) ++allocated;
        checkInQueue.clear();
        audit.append("Seating allocation completed: " + std::to_string(allocated) + " of " + std::to_string(candidateCount) + " candidates assigned.");
        std::cout << "Allocation complete: " << allocated << " allocated; " << (candidateCount - allocated) << " unallocated.\n";
        std::cout << "Use the dashboard and centre reports to review capacity.\n";
    }

    void viewSeatingPlan() const {
        listCentres();
        const std::string id = readLine("Centre ID: ");
        const int cidx = findCentreById(id);
        if (cidx == -1) { std::cout << "Unknown centre.\n"; return; }
        const Centre& centre = centres[cidx];
        if (centre.hallCount == 0) { std::cout << "No halls configured.\n"; return; }
        const int hallNo = readInt("Hall number: ", 1, centre.hallCount);
        const Hall& hall = centre.halls[hallNo - 1];
        std::cout << "\n" << centre.name << " / Hall " << hallNo << " / " << hall.rows << " x " << hall.cols << "\n";
        std::cout << "Each occupied cell shows the last 4 characters of the application ID.\n\n";
        std::cout << std::setw(5) << "ROW";
        for (int col = 0; col < hall.cols; ++col) std::cout << std::setw(7) << (col + 1);
        std::cout << '\n' << std::string(5 + 7 * hall.cols, '-') << '\n';
        for (int r = 0; r < hall.rows; ++r) {
            std::cout << std::setw(5) << (r + 1);
            for (int col = 0; col < hall.cols; ++col) {
                const int ci = hall.seats[r][col];
                if (ci == -1) std::cout << std::setw(7) << "·";
                else {
                    const std::string& appId = candidates[ci].id;
                    const std::string label = appId.size() <= 4 ? appId : appId.substr(appId.size() - 4);
                    std::cout << std::setw(7) << label;
                }
            }
            std::cout << '\n';
        }
    }

    void prepareCheckInQueue() {
        if (occupiedSeatCount() == 0) { std::cout << "Allocate seats before preparing the check-in queue.\n"; return; }
        checkInQueue.clear();
        int queued = 0;
        for (int i = 0; i < candidateCount; ++i) {
            if (candidates[i].allocated && !candidates[i].checkedIn && checkInQueue.enqueue(i)) ++queued;
        }
        audit.append("Check-in queue prepared with " + std::to_string(queued) + " candidates.");
        std::cout << "Check-in queue ready: " << queued << " candidates, processed FIFO.\n";
    }

    void processNextCheckIn() {
        int idx = -1;
        if (!checkInQueue.dequeue(idx)) { std::cout << "Check-in queue is empty. Prepare it or verify attendance.\n"; return; }
        if (idx < 0 || idx >= candidateCount || candidates[idx].checkedIn || !candidates[idx].allocated) {
            std::cout << "Queue item is no longer eligible. Prepare the queue again.\n"; return;
        }
        candidates[idx].checkedIn = true;
        if (checkInHistoryCount < MAX_CANDIDATES) checkInHistory[checkInHistoryCount++] = idx;
        audit.append("Candidate checked in: " + candidates[idx].id + " (" + candidates[idx].name + ").");
        std::cout << "Checked in: " << candidates[idx].id << " — " << candidates[idx].name << '\n';
        if (candidates[idx].allocated) {
            const Centre& c = centres[candidates[idx].centreIndex];
            std::cout << "Seat: " << c.city << ", Hall " << (candidates[idx].hallIndex + 1)
                      << ", Row " << (candidates[idx].row + 1) << ", Seat " << (candidates[idx].col + 1) << '\n';
        }
    }

    void undoLastCheckIn() {
        if (checkInHistoryCount == 0) { std::cout << "No check-in action is available to undo in this session.\n"; return; }
        const int idx = checkInHistory[--checkInHistoryCount];
        if (idx < 0 || idx >= candidateCount || !candidates[idx].checkedIn) { std::cout << "Nothing to undo for that candidate.\n"; return; }
        candidates[idx].checkedIn = false;
        checkInQueue.clear(); // prevent stale ordering; coordinator must rebuild FIFO queue
        audit.append("Check-in reversed for " + candidates[idx].id + "; queue cleared and must be prepared again.");
        std::cout << "Last check-in reversed for " << candidates[idx].id << ". Prepare the queue again before processing more candidates.\n";
    }

    void simulateCentreFailure() {
        if (planLocked) { std::cout << "Unlock the plan before simulating a centre failure.\n"; return; }
        if (hasAnyCheckIns()) { std::cout << "Centre failure simulation is only available before check-in begins.\n"; return; }
        listCentres();
        const std::string id = readLine("Centre ID to mark unavailable: ");
        const int failed = findCentreById(id);
        if (failed == -1) { std::cout << "Unknown centre.\n"; return; }
        if (!centres[failed].available) { std::cout << "Centre is already offline.\n"; return; }
        if (!confirm("Simulate failure of " + centres[failed].name + " and attempt to reallocate its candidates")) return;

        undoStack.push(makeSnapshot("centre failure simulation at " + centres[failed].id));
        centres[failed].available = false;
        int displaced = 0;
        for (int i = 0; i < candidateCount; ++i) {
            Candidate& c = candidates[i];
            if (c.allocated && c.centreIndex == failed) {
                c.allocated = false;
                c.centreIndex = c.hallIndex = c.row = c.col = -1;
                ++displaced;
            }
        }
        rebuildSeatGrids();
        int reallocated = 0;
        // Restore the list of displaced candidates from the snapshot; candidates that were already
        // unallocated before the failure are left in the exception report.
        UndoSnapshot* topSnapshot = nullptr;
        // The stack intentionally owns its snapshot; reallocation is performed for any now-unallocated candidate
        // whose former assignment points to the failed centre, identified through the saved state copy.
        // Use a local saved-state copy from the snapshot before proceeding by temporarily popping it.
        // (The top item is returned to the stack after we inspect it.)
        // To keep the implementation simple, displaced candidate IDs are reconstructed from the audit-relevant
        // condition captured just before clearing the seats.
        // We use a second pass over the pre-failure seat grid encoded in the most recent snapshot.
        // UndoStack offers pop; re-push is safe and preserves LIFO order.
        topSnapshot = undoStack.pop();
        if (topSnapshot) {
            for (int i = 0; i < std::min(candidateCount, topSnapshot->candidateCount); ++i) {
                if (topSnapshot->candidates[i].allocated && topSnapshot->candidates[i].centreIndex == failed) {
                    if (allocateCandidate(i)) ++reallocated;
                }
            }
            undoStack.push(topSnapshot);
        }
        checkInQueue.clear();
        const int stranded = displaced - reallocated;
        audit.append("Centre failure simulated at " + centres[failed].id + ": " + std::to_string(displaced) +
                     " displaced, " + std::to_string(reallocated) + " reallocated, " + std::to_string(stranded) + " still unallocated.");
        std::cout << "\nCENTRE FAILURE REPORT\n" << std::string(34, '-') << '\n';
        std::cout << "Candidates displaced : " << displaced << '\n';
        std::cout << "Reallocated          : " << reallocated << '\n';
        std::cout << "Still unallocated    : " << stranded << '\n';
        std::cout << "Other candidates who were already unallocated remain in the exception report.\n";
        std::cout << "Use Undo Last Allocation Change to restore the previous state.\n";
    }

    void undoAllocationChange() {
        if (planLocked) { std::cout << "The plan is locked. Unlock it before undoing allocation changes.\n"; return; }
        UndoSnapshot* snapshot = undoStack.pop();
        if (!snapshot) { std::cout << "No allocation change is available to undo.\n"; return; }
        const std::string label = snapshot->label;
        restoreSnapshot(snapshot);
        std::cout << "Undid: " << label << ".\n";
    }

    void togglePlanLock() {
        if (!planLocked) {
            if (candidateCount > 0 && occupiedSeatCount() == 0) {
                std::cout << "Allocate candidates before locking the seating plan.\n"; return;
            }
            if (candidateCount > occupiedSeatCount()) {
                std::cout << "Warning: " << (candidateCount - occupiedSeatCount()) << " candidates are unallocated.\n";
                if (!confirm("Lock anyway")) return;
            }
            if (!confirm("Lock the seating plan? Allocation edits and centre-failure simulation will be disabled")) return;
            planLocked = true;
            audit.append("Seating plan LOCKED by operator.");
            std::cout << "Seating plan locked. Use Unlock Plan for an authorised correction.\n";
        } else {
            const std::string code = readLine("Enter UNLOCK to unlock the seating plan: ");
            if (code != "UNLOCK") { std::cout << "Unlock cancelled.\n"; return; }
            planLocked = false;
            audit.append("Seating plan unlocked for authorised changes.");
            std::cout << "Seating plan unlocked. Changes will remain in the audit trail.\n";
        }
    }

    void showAudit() const {
        std::cout << "\nRECENT AUDIT EVENTS (newest shown within last 25)\n" << std::string(58, '-') << '\n';
        audit.print(25);
    }

    void exportReports() const {
        {
            std::ofstream out("candidates_export.csv");
            if (!out) { std::cout << "Could not write candidates_export.csv.\n"; return; }
            out << "candidate_id,name,preference1,preference2,assigned_centre,hall,row,seat,allocated,checked_in\n";
            for (int i = 0; i < candidateCount; ++i) {
                const Candidate& c = candidates[i];
                const std::string centre = c.allocated ? centres[c.centreIndex].id : "";
                out << csvEscape(c.id) << ',' << csvEscape(c.name) << ',' << csvEscape(c.preference1) << ',' << csvEscape(c.preference2) << ','
                    << csvEscape(centre) << ',' << (c.allocated ? c.hallIndex + 1 : 0) << ',' << (c.allocated ? c.row + 1 : 0) << ','
                    << (c.allocated ? c.col + 1 : 0) << ',' << (c.allocated ? "yes" : "no") << ',' << (c.checkedIn ? "yes" : "no") << '\n';
            }
        }
        {
            std::ofstream out("seating_report.csv");
            if (!out) { std::cout << "Could not write seating_report.csv.\n"; return; }
            out << "centre_id,centre_name,city,hall,row,seat,candidate_id,candidate_name\n";
            for (int c = 0; c < centreCount; ++c) {
                for (int h = 0; h < centres[c].hallCount; ++h) {
                    const Hall& hall = centres[c].halls[h];
                    for (int r = 0; r < hall.rows; ++r) for (int col = 0; col < hall.cols; ++col) {
                        const int idx = hall.seats[r][col];
                        if (idx == -1) continue;
                        out << csvEscape(centres[c].id) << ',' << csvEscape(centres[c].name) << ',' << csvEscape(centres[c].city) << ','
                            << h + 1 << ',' << r + 1 << ',' << col + 1 << ',' << csvEscape(candidates[idx].id) << ','
                            << csvEscape(candidates[idx].name) << '\n';
                    }
                }
            }
        }
        {
            std::ofstream out("audit_log.txt");
            if (out) audit.save(out);
        }
        std::cout << "Reports exported: candidates_export.csv, seating_report.csv, audit_log.txt\n";
    }

    void printMenu() const {
        std::cout << "\n" << std::string(62, '=') << '\n';
        std::cout << " EXAMGRID | Examination Centre & Seating Operations Simulator\n";
        std::cout << std::string(62, '=') << '\n';
        std::cout << " 1. Operations dashboard\n";
        std::cout << " 2. Generate/reset demo dataset (600 synthetic candidates)\n";
        std::cout << " 3. Add candidate\n";
        std::cout << " 4. Import candidates from CSV\n";
        std::cout << " 5. List candidates (BST inorder)\n";
        std::cout << " 6. Search candidate by application ID (BST)\n";
        std::cout << " 7. List centres and capacity\n";
        std::cout << " 8. Add examination centre\n";
        std::cout << " 9. Allocate/rebuild seating plan\n";
        std::cout << "10. View a hall's seating grid\n";
        std::cout << "11. Prepare check-in queue\n";
        std::cout << "12. Process next check-in (FIFO queue)\n";
        std::cout << "13. Undo last check-in (stack)\n";
        std::cout << "14. Simulate centre failure and reallocate\n";
        std::cout << "15. Undo last allocation change (stack)\n";
        std::cout << "16. Lock/unlock seating plan\n";
        std::cout << "17. View audit trail (linked list)\n";
        std::cout << "18. Export CSV reports\n";
        std::cout << "19. Save now\n";
        std::cout << " 0. Save and exit\n";
    }

    void run() {
        std::cout << "ExamGrid ready. Data is synthetic and for educational simulation only.\n";
        while (true) {
            printMenu();
            const int choice = readInt("Select an option: ", 0, 19);
            switch (choice) {
                case 1: showDashboard(); break;
                case 2:
                    if (confirm("Replace current data with the 600-record demo dataset")) generateDemoData();
                    break;
                case 3: addCandidateInteractive(); break;
                case 4: importCandidatesCsv(); break;
                case 5: listCandidates(); break;
                case 6: searchCandidate(); break;
                case 7: listCentres(); break;
                case 8: addCentre(); break;
                case 9: allocateAll(); break;
                case 10: viewSeatingPlan(); break;
                case 11: prepareCheckInQueue(); break;
                case 12: processNextCheckIn(); break;
                case 13: undoLastCheckIn(); break;
                case 14: simulateCentreFailure(); break;
                case 15: undoAllocationChange(); break;
                case 16: togglePlanLock(); break;
                case 17: showAudit(); break;
                case 18: exportReports(); break;
                case 19:
                    if (saveState(STATE_FILE)) std::cout << "Saved to " << STATE_FILE << ".\n";
                    else std::cout << "Could not save state.\n";
                    break;
                case 0:
                    if (saveState(STATE_FILE)) std::cout << "Saved state to " << STATE_FILE << ". Goodbye.\n";
                    else std::cout << "Warning: save failed. Goodbye.\n";
                    return;
            }
        }
    }
};
// Minimal single-process HTTP server: browser UI is static HTML/CSS/JS and all DSA state lives in ExamGrid.
#ifndef _WIN32
namespace {
struct HttpResponse { int status = 200; std::string type = "application/json; charset=utf-8"; std::string body; };

std::string urlDecode(const std::string& input) {
    std::string out;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '+' ) out += ' ';
        else if (input[i] == '%' && i + 2 < input.size()) {
            auto hex = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
            int a = hex(input[i + 1]), b = hex(input[i + 2]);
            if (a >= 0 && b >= 0) { out += static_cast<char>((a << 4) | b); i += 2; } else out += input[i];
        } else out += input[i];
    }
    return out;
}

std::map<std::string, std::string> parseParams(const std::string& input) {
    std::map<std::string, std::string> result;
    std::size_t start = 0;
    while (start <= input.size()) {
        const std::size_t end = input.find('&', start);
        const std::string pair = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const std::size_t eq = pair.find('=');
        if (!pair.empty()) result[urlDecode(pair.substr(0, eq))] = eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

std::string getParam(const std::map<std::string, std::string>& params, const std::string& key, const std::string& fallback = "") {
    auto it = params.find(key); return it == params.end() ? fallback : it->second;
}

std::string contentTypeFor(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css; charset=utf-8";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "application/javascript; charset=utf-8";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") return "application/json; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".csv") return "text/csv; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") return "image/svg+xml";
    return "application/octet-stream";
}

HttpResponse jsonResponse(const std::string& body, int status = 200) { return {status, "application/json; charset=utf-8", body}; }

void sendAll(int socketFd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t amount = send(socketFd, data.data() + sent, data.size() - sent, 0);
        if (amount <= 0) return;
        sent += static_cast<std::size_t>(amount);
    }
}

HttpResponse routeRequest(ExamGrid& app, const std::string& method, const std::string& target, const std::string& body) {
    const std::size_t qmark = target.find('?');
    const std::string path = qmark == std::string::npos ? target : target.substr(0, qmark);
    const auto query = parseParams(qmark == std::string::npos ? "" : target.substr(qmark + 1));
    const auto form = parseParams(body);
    auto persist = [&app](const std::string& json) { if (json.find("\"ok\":true") != std::string::npos) app.saveState(STATE_FILE); return jsonResponse(json); };

    if (method == "GET" && path == "/api/dashboard") return jsonResponse(app.apiDashboard());
    if (method == "GET" && path == "/api/centres") return jsonResponse("{\"centres\":" + app.apiCentresArray() + "}");
    if (method == "GET" && path == "/api/candidates") {
        int limit = 100; try { limit = std::stoi(getParam(query, "limit", "100")); } catch (...) {}
        limit = std::max(1, std::min(200, limit));
        return jsonResponse(app.apiCandidates(getParam(query, "q"), limit));
    }
    if (method == "GET" && path == "/api/candidate") return jsonResponse(app.apiSearchCandidate(getParam(query, "id")));
    if (method == "GET" && path == "/api/seats") {
        int hall = 1; try { hall = std::stoi(getParam(query, "hall", "1")); } catch (...) {}
        return jsonResponse(app.apiSeatMap(getParam(query, "centre", "C001"), hall));
    }
    if (method == "GET" && path == "/api/audit") return jsonResponse(app.apiAudit());
    if (method == "GET" && path == "/api/queue") return jsonResponse(app.apiQueue());
    if (method == "GET" && path == "/api/export/candidates.csv") return {200, "text/csv; charset=utf-8", app.apiExportCandidates()};
    if (method == "GET" && path == "/api/export/seating.csv") return {200, "text/csv; charset=utf-8", app.apiExportSeats()};

    if (method == "POST" && path == "/api/allocate") return persist(app.apiAllocate());
    if (method == "POST" && path == "/api/candidates/add") return persist(app.apiAddCandidate(getParam(form, "id"), getParam(form, "name"), getParam(form, "preference1"), getParam(form, "preference2")));
    if (method == "POST" && path == "/api/centres/add") {
        int halls = 1, rows = 10, cols = 10;
        try { halls = std::stoi(getParam(form, "halls", "1")); rows = std::stoi(getParam(form, "rows", "10")); cols = std::stoi(getParam(form, "cols", "10")); } catch (...) {}
        return persist(app.apiAddCentre(getParam(form, "id"), getParam(form, "name"), getParam(form, "city"), halls, rows, cols));
    }
    if (method == "POST" && path == "/api/import") return persist(app.apiImportCsv(getParam(form, "csv")));
    if (method == "POST" && path == "/api/queue/prepare") return persist(app.apiPrepareQueue());
    if (method == "POST" && path == "/api/queue/process") return persist(app.apiProcessCheckIn());
    if (method == "POST" && path == "/api/checkin/undo") return persist(app.apiUndoCheckIn());
    if (method == "POST" && path == "/api/centre/fail") return persist(app.apiSimulateCentreFailure(getParam(form, "centreId")));
    if (method == "POST" && path == "/api/allocation/undo") return persist(app.apiUndoAllocation());
    if (method == "POST" && path == "/api/plan/lock") {
        const bool locked = getParam(form, "locked") == "1";
        const bool allowPartial = getParam(form, "allowPartial") == "1";
        return persist(app.apiSetLock(locked, allowPartial));
    }
    if (method == "POST" && path == "/api/reset") return persist(app.apiResetDemo());

    if (method == "GET" || method == "HEAD") {
        std::string relative = urlDecode(path);
        if (relative.empty() || relative == "/") relative = "/index.html";
        if (relative.find("..") != std::string::npos || relative.find('\\') != std::string::npos) return {400, "text/plain; charset=utf-8", "Bad path"};
        const std::string filePath = "web" + relative;
        std::ifstream file(filePath, std::ios::binary);
        if (!file) return {404, "text/plain; charset=utf-8", "Not found"};
        std::ostringstream contents; contents << file.rdbuf();
        return {200, contentTypeFor(filePath), method == "HEAD" ? "" : contents.str()};
    }
    return jsonResponse(apiResult(false, "Endpoint not found."), 404);
}

void serveClient(int client, ExamGrid& app) {
    std::string request;
    char buffer[8192];
    std::size_t headerEnd = std::string::npos;
    std::size_t contentLength = 0;
    while (request.size() < 2 * 1024 * 1024) {
        const ssize_t got = recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0) break;
        request.append(buffer, static_cast<std::size_t>(got));
        headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const std::string headers = request.substr(0, headerEnd);
            std::istringstream h(headers);
            std::string line;
            std::getline(h, line);
            while (std::getline(h, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::string low = lowerCopy(line);
                if (low.rfind("content-length:", 0) == 0) {
                    try { contentLength = static_cast<std::size_t>(std::stoul(line.substr(line.find(':') + 1))); } catch (...) { contentLength = 0; }
                }
            }
            if (request.size() >= headerEnd + 4 + contentLength) break;
        }
    }
    HttpResponse response;
    if (headerEnd == std::string::npos) response = {400, "text/plain; charset=utf-8", "Bad request"};
    else {
        const std::string header = request.substr(0, headerEnd);
        std::istringstream h(header);
        std::string line, method = "GET", target = "/", version;
        h >> method >> target >> version;
        const std::string body = request.substr(headerEnd + 4, contentLength);
        response = routeRequest(app, method, target, body);
    }
    std::ostringstream head;
    const char* statusText = response.status == 200 ? "OK" : response.status == 400 ? "Bad Request" : response.status == 404 ? "Not Found" : "Error";
    head << "HTTP/1.1 " << response.status << ' ' << statusText << "\r\n"
         << "Content-Type: " << response.type << "\r\n"
         << "Content-Length: " << response.body.size() << "\r\n"
         << "Connection: close\r\n"
         << "Cache-Control: no-store\r\n"
         << "X-Content-Type-Options: nosniff\r\n\r\n";
    const std::string headText = head.str();
    sendAll(client, headText);
    if (!response.body.empty()) sendAll(client, response.body);
    close(client);
}
}
#endif
} // namespace

int main() {
#ifdef _WIN32
    std::cerr << "The built-in web server currently targets macOS/Linux (POSIX sockets).\n";
    return 1;
#else
    auto app = std::make_unique<ExamGrid>();
    if (app->loadState(STATE_FILE)) {
        std::cout << "Loaded saved ExamGrid state from " << STATE_FILE << ".\n";
    } else {
        app->generateDemoData();
        app->saveState(STATE_FILE);
    }
    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { std::cerr << "Could not create server socket: " << std::strerror(errno) << "\n"; return 1; }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int port = 8080;
    if (const char* portEnv = std::getenv("EXAMGRID_PORT")) {
        try { const int requested = std::stoi(portEnv); if (requested >= 1024 && requested <= 65535) port = requested; } catch (...) {}
    }
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "Could not bind to 127.0.0.1:" << port << ". Try another EXAMGRID_PORT. " << std::strerror(errno) << "\n";
        close(server); return 1;
    }
    if (listen(server, 16) < 0) { std::cerr << "Could not listen on port " << port << ".\n"; close(server); return 1; }
    std::cout << "\nExamGrid website is running at http://127.0.0.1:" << port << "\nPress Ctrl+C to stop the server.\n";
    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        const int client = accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (client < 0) { if (errno == EINTR) continue; std::cerr << "Accept failed: " << std::strerror(errno) << "\n"; continue; }
        serveClient(client, *app);
    }
    close(server);
    return 0;
#endif
}
