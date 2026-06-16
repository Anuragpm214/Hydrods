#include "hydrodb_engine.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <fcntl.h>
#include <sys/epoll.h>
#include <csignal>

using namespace std;

// Initialize the global core engine
HydroDBEngine<string, string> db(1000);

// Persistence variables
const string AOF_FILENAME = "hydrodb.aof";
mutex aof_mutex;
ofstream aof_file;

// Asynchronous AOF variables
queue<string> aof_queue;
condition_variable aof_cv;

// Epoll Client Buffers
unordered_map<int, string> client_buffers;       // Read buffers (input)
unordered_map<int, string> client_write_buffers;  // Write buffers (output — EPOLLOUT)

void aof_writer_thread_func() {
    while (true) {
        queue<string> local_queue;
        {
            unique_lock<mutex> lock(aof_mutex);
            aof_cv.wait(lock, []{ return !aof_queue.empty(); });
            swap(aof_queue, local_queue);
        }
        
        if (aof_file.is_open()) {
            bool wrote = false;
            while (!local_queue.empty()) {
                aof_file << local_queue.front() << "\n";
                local_queue.pop();
                wrote = true;
            }
            if (wrote) {
                aof_file.flush();
            }
        }
    }
}

void replay_aof() {
    ifstream infile(AOF_FILENAME);
    if (!infile.is_open()) return;
    
    cout << "Loading AOF file for recovery..." << endl;
    string line;
    int restored = 0;
    while (getline(infile, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        string cmd, key, val;
        ss >> cmd >> key;
        
        if (cmd == "SET") {
            getline(ss, val);
            if (!val.empty() && val.front() == ' ') val.erase(0, 1);
            db.set(key, val);
            restored++;
        } else if (cmd == "DEL") {
            db.erase(key);
            restored++;
        }
    }
    cout << "Restored " << restored << " commands from AOF." << endl;
}

void log_aof(const string& command) {
    {
        lock_guard<mutex> lock(aof_mutex);
        aof_queue.push(command);
    }
    aof_cv.notify_one();
}

// ========================================
// EPOLLOUT Write Buffering
// ========================================

// Try to flush write buffer for a client.
// Returns true if buffer is fully flushed (or was already empty).
bool try_flush_writes(int client_sock, int epoll_fd) {
    auto it = client_write_buffers.find(client_sock);
    if (it == client_write_buffers.end() || it->second.empty()) return true;

    string& wbuf = it->second;
    while (!wbuf.empty()) {
        ssize_t ret = write(client_sock, wbuf.c_str(), wbuf.size());
        if (ret > 0) {
            wbuf.erase(0, ret);
        } else if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full — register EPOLLOUT to resume later
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                ev.data.fd = client_sock;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_sock, &ev);
                return false;
            }
            // Real write error
            return false;
        } else {
            break; // ret == 0, shouldn't happen for TCP
        }
    }

    // All flushed — switch back to EPOLLIN only
    if (wbuf.empty()) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_sock;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_sock, &ev);
        client_write_buffers.erase(it);
        return true;
    }
    return false;
}

// ========================================
// Command Processing
// ========================================

// Process a command, building the response into `response`.
// Returns false if the connection should be closed after sending the response.
bool process_command(vector<string>& args, string& response) {
    if (args.empty()) return true;
    string cmd = args[0];
    for(auto &c : cmd) c = toupper(c);
    
    try {
        if (cmd == "SET") {
            if (args.size() < 3) response = "-ERR wrong number of arguments for 'set' command\r\n";
            else {
                db.set(args[1], args[2]);
                log_aof("SET " + args[1] + " " + args[2]);
                response = "+OK\r\n";
            }
        } else if (cmd == "GET") {
            if (args.size() < 2) response = "-ERR wrong number of arguments for 'get' command\r\n";
            else {
                auto opt = db.get(args[1]);
                if (opt) response = "$" + to_string(opt->size()) + "\r\n" + *opt + "\r\n";
                else response = "$-1\r\n";
            }
        } else if (cmd == "DEL") {
            if (args.size() < 2) response = "-ERR wrong number of arguments for 'del' command\r\n";
            else {
                bool ok = db.erase(args[1]);
                if (ok) log_aof("DEL " + args[1]);
                response = ok ? ":1\r\n" : ":0\r\n";
            }
        } else if (cmd == "HRANGE") {
            if (args.size() < 3) response = "-ERR wrong number of arguments for 'hrange' command\r\n";
            else {
                string l = args[1], r = args[2];
                int offset = 0, count = -1;
                if (args.size() >= 6) {
                    string token = args[3];
                    for(auto &c : token) c = toupper(c);
                    if (token == "LIMIT") {
                        offset = stoi(args[4]);
                        count = stoi(args[5]);
                    }
                }
                auto all_res = db.range(l, r, offset, count);

                // FIX Bug #1: Replace ostringstream with pre-reserved string::append
                size_t total_size = 16; // header estimate
                for (const auto& p : all_res) {
                    total_size += 6 + p.first.size() + 6 + p.second.size();
                }
                response.clear();
                response.reserve(total_size);

                response += "*";
                response += to_string(all_res.size() * 2);
                response += "\r\n";
                for (const auto& p : all_res) {
                    response += "$";
                    response += to_string(p.first.size());
                    response += "\r\n";
                    response += p.first;
                    response += "\r\n";
                    response += "$";
                    response += to_string(p.second.size());
                    response += "\r\n";
                    response += p.second;
                    response += "\r\n";
                }
            }
        } else if (cmd == "DBSIZE") {
            // O(1) count — no need to fetch all elements
            response = ":" + to_string(db.count()) + "\r\n";
        } else if (cmd == "HMIN") {
            // O(1) minimum — reads first element of first bucket
            auto entry = db.min_entry();
            if (entry) {
                response.clear();
                response += "*2\r\n$";
                response += to_string(entry->first.size());
                response += "\r\n";
                response += entry->first;
                response += "\r\n$";
                response += to_string(entry->second.size());
                response += "\r\n";
                response += entry->second;
                response += "\r\n";
            } else {
                response = "*0\r\n";
            }
        } else if (cmd == "HMAX") {
            // O(1) maximum — reads last element of last bucket
            auto entry = db.max_entry();
            if (entry) {
                response.clear();
                response += "*2\r\n$";
                response += to_string(entry->first.size());
                response += "\r\n";
                response += entry->first;
                response += "\r\n$";
                response += to_string(entry->second.size());
                response += "\r\n";
                response += entry->second;
                response += "\r\n";
            } else {
                response = "*0\r\n";
            }
        } else if (cmd == "PING") {
            response = "+PONG\r\n";
        } else if (cmd == "HELLO") {
            response = "+OK\r\n";
        } else if (cmd == "QUIT") {
            response = "+OK\r\n";
            return false;
        } else {
            response = "-ERR unknown command '" + cmd + "'\r\n";
        }
    } catch (const exception& e) {
        response = string("-ERR ") + e.what() + "\r\n";
    }
    
    return true;
}

// ========================================
// Client Data Handler
// ========================================

void handle_client_data(int client_sock, int epoll_fd) {
    char buffer[4096];
    bool should_close = false;
    
    // Read all available non-blocking data
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = read(client_sock, buffer, sizeof(buffer) - 1);
        
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // No more data right now
            }
            should_close = true; // Error reading
            break;
        } else if (bytes_read == 0) {
            should_close = true; // Client disconnected
            break;
        }
        
        client_buffers[client_sock] += buffer;
        if (client_buffers[client_sock].size() > 1024 * 1024) { 
            client_write_buffers[client_sock] += "-ERR command too long\r\n";
            try_flush_writes(client_sock, epoll_fd);
            should_close = true;
            break;
        }
    }
    
    string& unparsed_buffer = client_buffers[client_sock];
    
    // Parse commands
    try {
        while (!unparsed_buffer.empty() && !should_close) {
            if (unparsed_buffer[0] == '*') {
                size_t pos = unparsed_buffer.find("\r\n");
                if (pos == string::npos) break;
                
                int num_args;
                try {
                    num_args = stoi(unparsed_buffer.substr(1, pos - 1));
                } catch(...) {
                    should_close = true; break;
                }
                
                size_t current_pos = pos + 2;
                vector<string> args;
                bool incomplete = false;
                
                for (int i = 0; i < num_args; ++i) {
                    if (current_pos >= unparsed_buffer.size() || unparsed_buffer[current_pos] != '$') {
                        incomplete = true; break;
                    }
                    size_t len_pos = unparsed_buffer.find("\r\n", current_pos);
                    if (len_pos == string::npos) { incomplete = true; break; }
                    
                    int arg_len;
                    try {
                        arg_len = stoi(unparsed_buffer.substr(current_pos + 1, len_pos - current_pos - 1));
                    } catch(...) {
                        incomplete = true; should_close = true; break;
                    }
                    
                    size_t str_start = len_pos + 2;
                    size_t str_end = str_start + arg_len;
                    
                    if (str_end + 2 > unparsed_buffer.size()) { incomplete = true; break; }
                    
                    args.push_back(unparsed_buffer.substr(str_start, arg_len));
                    current_pos = str_end + 2;
                }
                
                if (incomplete && !should_close) break;
                if (should_close) break;
                
                unparsed_buffer.erase(0, current_pos);

                // FIX Bug #2: Buffer response instead of writing directly
                string response;
                bool keep_alive = process_command(args, response);
                client_write_buffers[client_sock] += response;
                if (!keep_alive) {
                    try_flush_writes(client_sock, epoll_fd);
                    should_close = true;
                }
            } else {
                size_t pos = unparsed_buffer.find('\n');
                if (pos == string::npos) break;
                
                string line = unparsed_buffer.substr(0, pos);
                unparsed_buffer.erase(0, pos + 1);
                
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                
                stringstream ss(line);
                string token;
                vector<string> args;
                while (ss >> token) args.push_back(token);
                
                if (!args.empty()) {
                    string response;
                    bool keep_alive = process_command(args, response);
                    client_write_buffers[client_sock] += response;
                    if (!keep_alive) {
                        try_flush_writes(client_sock, epoll_fd);
                        should_close = true;
                    }
                }
            }
        }
    } catch (...) {
        should_close = true;
    }

    // Flush all accumulated responses after processing
    if (!should_close) {
        try_flush_writes(client_sock, epoll_fd);
    }
    
    if (should_close) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sock, nullptr);
        close(client_sock);
        client_buffers.erase(client_sock);
        client_write_buffers.erase(client_sock);
    }
}

int main() {
    signal(SIGPIPE, SIG_IGN); 
    
    // 1. Replay AOF
    replay_aof();
    
    // 2. Open AOF file
    aof_file.open(AOF_FILENAME, ios::app);
    if (!aof_file.is_open()) {
        cerr << "Failed to open AOF file for writing!" << endl;
        return 1;
    }
    
    // Start background AOF writer
    thread(aof_writer_thread_func).detach();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        cerr << "Failed to create socket." << endl;
        return 1;
    }
    
    // Make server_fd non-blocking
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(7379);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        cerr << "Bind failed. Port 7379 might be in use." << endl;
        return 1;
    }
    
    if (listen(server_fd, 1024) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    // Setup EPOLL
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }
    
    struct epoll_event event;
    event.events = EPOLLIN; // Level triggered for server accept
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl");
        return 1;
    }
    
    cout << "==============================================" << endl;
    cout << "  🌊 HydroDB Server v1.1 (Optimized)        " << endl;
    cout << "  Listening on TCP port 7379...               " << endl;
    cout << "  EPOLL Event Loop:   ENABLED                " << endl;
    cout << "  EPOLLOUT Buffering: ENABLED                " << endl;
    cout << "  AOF Persistence:    ENABLED                " << endl;
    cout << "  Commands: SET GET DEL HRANGE PING QUIT     " << endl;
    cout << "            DBSIZE HMIN HMAX                 " << endl;
    cout << "==============================================" << endl;
    
    const int MAX_EVENTS = 1024;
    struct epoll_event events[MAX_EVENTS];
    
    while (true) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                // Accept all incoming connections
                while (true) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    
                    if (client_sock == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break; // No more
                        else { perror("accept"); break; }
                    }
                    
                    // Make client non-blocking
                    int flags = fcntl(client_sock, F_GETFL, 0);
                    fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);
                    
                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLET; // Edge-triggered
                    ev.data.fd = client_sock;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &ev);
                }
            } else if (events[i].events & EPOLLOUT) {
                // EPOLLOUT fired — resume flushing pending writes
                int fd = events[i].data.fd;
                if (!try_flush_writes(fd, epoll_fd)) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        // Real write error — close connection
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        client_buffers.erase(fd);
                        client_write_buffers.erase(fd);
                        continue;
                    }
                }
                // Also handle any readable data that arrived
                if (events[i].events & EPOLLIN) {
                    handle_client_data(events[i].data.fd, epoll_fd);
                }
            } else {
                handle_client_data(events[i].data.fd, epoll_fd);
            }
        }
    }
    return 0;
}
