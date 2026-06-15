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
unordered_map<int, string> client_buffers;

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

bool process_command(vector<string>& args, int client_sock) {
    if (args.empty()) return true;
    string cmd = args[0];
    for(auto &c : cmd) c = toupper(c);
    
    string response;
    try {
        if (cmd == "SET") {
            if (args.size() < 3) response = "-ERR wrong number of arguments for 'set' command\r\n";
            else {
                db.set(args[1], args[2]);
                log_aof("SET " + args[1] + " " + args[2]);
                response = "+OK\r\n";
            }
        } else if (cmd == "ZADD") {
            if (args.size() < 4) response = "-ERR wrong number of arguments for 'zadd' command\r\n";
            else {
                string composite_key = args[1] + ":" + args[2];
                db.set(composite_key, args[3]);
                log_aof("SET " + composite_key + " " + args[3]);
                response = ":1\r\n";
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
        } else if (cmd == "ZRANGE") {
            if (args.size() < 3) response = "-ERR wrong number of arguments for 'zrange' command\r\n";
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
                ostringstream oss;
                oss << "*" << (all_res.size() * 2) << "\r\n";
                for (auto& p : all_res) {
                    oss << "$" << p.first.size() << "\r\n" << p.first << "\r\n";
                    oss << "$" << p.second.size() << "\r\n" << p.second << "\r\n";
                }
                response = oss.str();
            }
        } else if (cmd == "PING") {
            response = "+PONG\r\n";
        } else if (cmd == "HELLO") {
            response = "+OK\r\n";
        } else if (cmd == "QUIT") {
            response = "+OK\r\n";
            int _ = write(client_sock, response.c_str(), response.size()); (void)_;
            return false;
        } else {
            response = "-ERR unknown command '" + cmd + "'\r\n";
        }
    } catch (const exception& e) {
        response = string("-ERR ") + e.what() + "\r\n";
    }
    
    // Write directly. In a robust epoll implementation, this would buffer to EPOLLOUT.
    int ret = write(client_sock, response.c_str(), response.size());
    if (ret <= 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        return false;
    }
    return true;
}

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
            string err = "-ERR command too long\r\n";
            int _ = write(client_sock, err.c_str(), err.size()); (void)_;
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
                if (!process_command(args, client_sock)) {
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
                    if (!process_command(args, client_sock)) {
                        should_close = true;
                    }
                }
            }
        }
    } catch (...) {
        should_close = true;
    }
    
    if (should_close) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sock, nullptr);
        close(client_sock);
        client_buffers.erase(client_sock);
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
    cout << "  🌊 HydroDB Prototype Server is running!   " << endl;
    cout << "  Listening on TCP port 7379...               " << endl;
    cout << "  EPOLL Event Loop is ENABLED.                " << endl;
    cout << "  AOF Persistence is ENABLED.                 " << endl;
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
            } else {
                handle_client_data(events[i].data.fd, epoll_fd);
            }
        }
    }
    return 0;
}
