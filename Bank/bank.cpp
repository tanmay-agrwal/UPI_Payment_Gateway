// bank.cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <ctime>
#include <mutex>
#include <vector>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <functional>  // Add this for std::bind
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/sha.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

const int BANK_PORT = 9000;
const char* BANK_HOST = "0.0.0.0";  // Listen on all network interfaces

mutex fileMutex;  // Protect file I/O
mutex consoleMutex;  // Protect console output

// Filenames for persistent storage
const string MERCHANTS_FILE = "merchants.json";
const string USERS_FILE = "users.json";
const string BLOCKCHAIN_FILE = "blockchain.json";
const string TRANSACTIONS_LOG_FILE = "transactions.log";

// Bank and branch information
struct Branch {
    string name;
    string ifsc;
};

struct Bank {
    string name;
    vector<Branch> branches;
};

// Initialize bank and branch data
vector<Bank> initializeBanks() {
    vector<Bank> banks;
    
    // HDFC Bank
    Bank hdfc;
    hdfc.name = "HDFC";
    hdfc.branches = {
        {"Main Branch", "HDFC0000001"},
        {"City Branch", "HDFC0000002"},
        {"Suburban Branch", "HDFC0000003"}
    };
    banks.push_back(hdfc);
    
    // ICICI Bank
    Bank icici;
    icici.name = "ICICI";
    icici.branches = {
        {"Main Branch", "ICIC0000001"},
        {"City Branch", "ICIC0000002"},
        {"Suburban Branch", "ICIC0000003"}
    };
    banks.push_back(icici);
    
    // SBI Bank
    Bank sbi;
    sbi.name = "SBI";
    sbi.branches = {
        {"Main Branch", "SBIN0000001"},
        {"City Branch", "SBIN0000002"},
        {"Suburban Branch", "SBIN0000003"}
    };
    banks.push_back(sbi);
    
    return banks;
}

// Global bank data
vector<Bank> banks = initializeBanks();

// Function to validate IFSC code
bool isValidIFSC(const string& ifsc) {
    for (const auto& bank : banks) {
        for (const auto& branch : bank.branches) {
            if (branch.ifsc == ifsc) {
                return true;
            }
        }
    }
    return false;
}

// Function to get bank name from IFSC
string getBankNameFromIFSC(const string& ifsc) {
    for (const auto& bank : banks) {
        for (const auto& branch : bank.branches) {
            if (branch.ifsc == ifsc) {
                return bank.name;
            }
        }
    }
    return "";
}

// Function to get branch name from IFSC
string getBranchNameFromIFSC(const string& ifsc) {
    for (const auto& bank : banks) {
        for (const auto& branch : bank.branches) {
            if (branch.ifsc == ifsc) {
                return branch.name;
            }
        }
    }
    return "";
}

// Function to get current timestamp as string
string getCurrentTimestamp() {
    auto now = chrono::system_clock::now();
    auto now_time = chrono::system_clock::to_time_t(now);
    stringstream ss;
    ss << put_time(localtime(&now_time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Function to log transaction details
void logTransaction(json& transaction, const string& user_name, const string& merchant_name) {
    lock_guard<mutex> consoleLock(consoleMutex);
    
    string timestamp = getCurrentTimestamp();
    string transaction_id = transaction["transaction_id"];
    string mmid = transaction["mmid"];
    string mid = transaction["mid"];
    double amount = transaction["amount"].get<double>();
    
    // Print transaction details to console
    cout << "\n===== TRANSACTION DETAILS =====" << endl;
    cout << "Timestamp: " << timestamp << endl;
    cout << "Transaction ID: " << transaction_id << endl;
    cout << "From: " << user_name << " (MMID: " << mmid << ")" << endl;
    cout << "To: " << merchant_name << " (MID: " << mid << ")" << endl;
    cout << "Amount: Rs. " << fixed << setprecision(2) << amount << endl;
    cout << "===============================" << endl;
    
    // Log transaction to file
    lock_guard<mutex> fileLock(fileMutex);
    ofstream logFile(TRANSACTIONS_LOG_FILE, ios::app);
    if (logFile.is_open()) {
        logFile << timestamp << " | " 
                << transaction_id << " | " 
                << user_name << " (" << mmid << ") | " 
                << merchant_name << " (" << mid << ") | " 
                << "Rs. " << fixed << setprecision(2) << amount << endl;
        logFile.close();
    }
}

// Load JSON data from file
json loadData(const string &filename) {
    lock_guard<mutex> lock(fileMutex);
    ifstream in(filename);
    if(in.good()){
        json data;
        in >> data;
        return data;
    }
    return json::object();
}

// Save JSON data to file
void saveData(const string &filename, const json &data) {
    lock_guard<mutex> lock(fileMutex);
    ofstream out(filename);
    out << setw(4) << data << endl;
}

// Compute SHA256 hash and return first 16 hex digits
string computeSHA256(const string &input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)input.c_str(), input.size(), hash);
    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    string fullHash = ss.str();
    return fullHash.substr(0, 16);
}

// Generate unique 16-digit hexadecimal ID from inputs and current time
string generateID(const vector<string> &inputs) {
    string combined;
    for (const auto &s : inputs) {
        combined += s;
    }
    combined += to_string(time(nullptr));
    return computeSHA256(combined);
}

// Process merchant registration (stores hashed password)
json registerMerchant(const json &req) {
    json merchants = loadData(MERCHANTS_FILE);
    string name = req["name"];
    string ifsc = req["ifsc"];
    string password = req["password"];
    string merchantId = req["merchant_id"];
    
    // Validate IFSC code
    if (!isValidIFSC(ifsc)) {
        return { {"status", "failure"}, {"reason", "Invalid IFSC code"} };
    }
    
    // Hash the password before storing
    string hashedPassword = computeSHA256(password);
    double initial_balance = req["initial_balance"].get<double>();
    
    // Store the merchant data using the merchant_id as the key
    merchants[merchantId] = {
        {"name", name},
        {"ifsc", ifsc},
        {"bank_name", getBankNameFromIFSC(ifsc)},
        {"branch_name", getBranchNameFromIFSC(ifsc)},
        {"password", hashedPassword},
        {"balance", initial_balance}
    };
    
    saveData(MERCHANTS_FILE, merchants);
    return { {"status", "success"}, {"mid", merchantId} };
}

// Process user registration (stores hashed password and transaction PIN)
json registerUser(const json &req) {
    json users = loadData(USERS_FILE);
    string name = req["name"];
    string mobile = req["mobile"];
    string password = req["password"];
    string transactionPin = req["transaction_pin"];
    string uid = req["user_id"];  // Treat user_id as uid
    
    // Validate IFSC code
    string ifsc = req["ifsc"];
    if (!isValidIFSC(ifsc)) {
        return { {"status", "failure"}, {"reason", "Invalid IFSC code"} };
    }
    
    // Hash the password and transaction PIN before storing
    string hashedPassword = computeSHA256(password);
    string hashedTransactionPin = computeSHA256(transactionPin);
    
    double initial_balance = req["initial_balance"].get<double>();
    string mmid = req["mmid"];
    users[mmid] = {
        {"name", name},
        {"uid", uid},  // Store as uid instead of user_id
        {"mobile", mobile},
        {"password", hashedPassword},
        {"transaction_pin", hashedTransactionPin},
        {"balance", initial_balance},
        {"ifsc", ifsc},
        {"bank_name", getBankNameFromIFSC(ifsc)},
        {"branch_name", getBranchNameFromIFSC(ifsc)}
    };
    saveData(USERS_FILE, users);
    return { {"status", "success"}, {"uid", uid}, {"mmid", mmid} };
}

// Check user details for pre-transaction verification
json checkUser(const json &req) {
    string mmid = req["mmid"];
    json users = loadData(USERS_FILE);
    if (users.find(mmid) == users.end())
        return { {"status", "failure"}, {"reason", "User not registered"} };
    return { {"status", "success"}, {"user", users[mmid]} };
}

// Check merchant details
json checkMerchant(const json &req) {
    string mid = req["mid"];
    json merchants = loadData(MERCHANTS_FILE);
    if (merchants.find(mid) == merchants.end())
        return { {"status", "failure"}, {"reason", "Merchant not registered"} };
    return { {"status", "success"}, {"merchant", merchants[mid]} };
}

// Return user's current balance (after verifying password)
json getUserBalance(const json &req) {
    // Handle requests with user_id only
    if (req.contains("user_id") && !req.contains("mmid")) {
        // Look up the user by uid first
        string uid = req["user_id"];  // Treat user_id as uid
        string password = req["password"];
        string hashedPassword = computeSHA256(password);
        
        json users = loadData(USERS_FILE);
        
        // Search for user with matching uid
        for (auto it = users.begin(); it != users.end(); ++it) {
            if (it.value().contains("uid") && it.value()["uid"] == uid) {
                if (it.value()["password"] != hashedPassword) {
                    return { {"status", "failure"}, {"reason", "Invalid password"} };
                }
                double balance = it.value()["balance"].get<double>();
                return { {"status", "success"}, {"balance", balance} };
            }
        }
        return { {"status", "failure"}, {"reason", "User not registered"} };
    }
    
    // Handle traditional mmid-based requests
    string mmid = req["mmid"];
    string password = req["password"];
    // If user_id is provided, verify it
    bool userIdProvided = req.contains("user_id");
    
    json users = loadData(USERS_FILE);
    if (users.find(mmid) == users.end())
        return { {"status", "failure"}, {"reason", "User not registered"} };
    
    // Verify uid if provided
    if (userIdProvided && users[mmid]["uid"] != req["user_id"]) {
        return { {"status", "failure"}, {"reason", "Invalid User ID"} };
    }
    
    if (users[mmid]["password"] != computeSHA256(password))
        return { {"status", "failure"}, {"reason", "Invalid password"} };
    
    double balance = users[mmid]["balance"].get<double>();
    return { {"status", "success"}, {"balance", balance} };
}

// Return merchant's current balance (after verifying password)
json getMerchantBalance(const json &req) {
    string merchantId = req["merchant_id"];
    string password = req["password"];
    string hashedPassword = computeSHA256(password);
    
    json merchants = loadData(MERCHANTS_FILE);
    
    // Search for merchant with matching merchant_id
    if (merchants.find(merchantId) != merchants.end()) {
        if (merchants[merchantId]["password"] == hashedPassword) {
            double balance = merchants[merchantId]["balance"].get<double>();
            return { {"status", "success"}, {"balance", balance} };
        }
        return { {"status", "failure"}, {"reason", "Invalid password"} };
    }
    return { {"status", "failure"}, {"reason", "Merchant not registered"} };
}

// Append a transaction block to the blockchain ledger
void addTransactionToBlockchain(const json &transaction) {
    json blockchain = loadData(BLOCKCHAIN_FILE);
    // Ensure blockchain is an array
    if (!blockchain.is_array()) {
        blockchain = json::array();
    }
    string prev_hash = blockchain.empty() ? "0" : blockchain.back()["block_hash"];
    json blockContent = {
        {"transaction", transaction},
        {"prev_hash", prev_hash},
        {"timestamp", chrono::system_clock::to_time_t(chrono::system_clock::now())}
    };
    string blockStr = blockContent.dump();
    string block_hash = computeSHA256(blockStr);
    json block = {
        {"transaction", transaction},
        {"prev_hash", prev_hash},
        {"timestamp", chrono::system_clock::to_time_t(chrono::system_clock::now())},
        {"block_hash", block_hash}
    };
    blockchain.push_back(block);
    saveData(BLOCKCHAIN_FILE, blockchain);
}

// Add new login functions for users and merchants
json loginUser(const json &req) {
    string uid = req["user_id"];  // Treat user_id as uid
    string password = req["password"];
    string hashedPassword = computeSHA256(password);
    
    json users = loadData(USERS_FILE);
    
    // Search for user with matching uid
    for (auto it = users.begin(); it != users.end(); ++it) {
        if (it.value().contains("uid") && it.value()["uid"] == uid && 
            it.value()["password"] == hashedPassword) {
            double balance = it.value()["balance"].get<double>();
            return { 
                {"status", "success"}, 
                {"mmid", it.key()}, 
                {"balance", balance} 
            };
        }
    }
    
    return { {"status", "failure"}, {"reason", "Invalid user ID or password"} };
}

json loginMerchant(const json &req) {
    string merchantId = req["merchant_id"];
    string password = req["password"];
    string hashedPassword = computeSHA256(password);
    
    json merchants = loadData(MERCHANTS_FILE);
    
    // Check if the merchant ID exists as a key in the merchants object
    if (merchants.find(merchantId) != merchants.end()) {
        // Verify password
        if (merchants[merchantId]["password"] == hashedPassword) {
            double balance = merchants[merchantId]["balance"].get<double>();
            return { 
                {"status", "success"}, 
                {"mid", merchantId}, 
                {"balance", balance} 
            };
        }
        return { {"status", "failure"}, {"reason", "Invalid password"} };
    }
    
    return { {"status", "failure"}, {"reason", "Invalid merchant ID or password"} };
}

// Process a transaction request (verifies transaction PIN)
json processTransaction(const json &req) {
    string mmid = req["mmid"];
    string transactionPin = req["transaction_pin"];
    string mid = req["mid"];
    double amount = req["amount"].get<double>();

    json users = loadData(USERS_FILE);
    json merchants = loadData(MERCHANTS_FILE);

    if (users.find(mmid) == users.end())
        return { {"status", "failure"}, {"reason", "User not registered"} };
    if (users[mmid]["transaction_pin"] != computeSHA256(transactionPin))
        return { {"status", "failure"}, {"reason", "Invalid transaction PIN"} };
    double user_balance = users[mmid]["balance"].get<double>();
    if (user_balance < amount)
        return { {"status", "failure"}, {"reason", "Insufficient funds"} };

    if (merchants.find(mid) == merchants.end())
        return { {"status", "failure"}, {"reason", "Merchant not registered"} };

    // Update balances
    users[mmid]["balance"] = user_balance - amount;
    double merchant_balance = merchants[mid]["balance"].get<double>();
    merchants[mid]["balance"] = merchant_balance + amount;
    saveData(USERS_FILE, users);
    saveData(MERCHANTS_FILE, merchants);

    // Create transaction record
    json transaction = {
        {"mmid", mmid},
        {"mid", mid},
        {"amount", amount},
        {"timestamp", (long)time(nullptr)}
    };
    transaction["transaction_id"] = generateID({ users[mmid]["uid"], mid, to_string(amount) });
    addTransactionToBlockchain(transaction);
    
    // Log transaction details
    string user_name = users[mmid]["name"];
    string merchant_name = merchants[mid]["name"];
    logTransaction(transaction, user_name, merchant_name);
    
    return { {"status", "success"}, {"transaction", transaction} };
}

// Handle incoming client connections
void handleClient(int clientSock) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    int bytes = read(clientSock, buffer, sizeof(buffer)-1);
    if (bytes <= 0) {
        close(clientSock);
        return;
    }
    try {
        json req = json::parse(string(buffer));
        json res;
        string type = req["type"];
        if (type == "register_merchant") {
            res = registerMerchant(req);
        } else if (type == "register_user") {
            res = registerUser(req);
        } else if (type == "check_user") {
            res = checkUser(req);
        } else if (type == "check_merchant") {
            res = checkMerchant(req);
        } else if (type == "transaction") {
            res = processTransaction(req);
        } else if (type == "get_balance_user") {
            res = getUserBalance(req);
        } else if (type == "get_balance_merchant") {
            res = getMerchantBalance(req);
        } else if (type == "login_user") {
            res = loginUser(req);
        } else if (type == "login_merchant") {
            res = loginMerchant(req);
        } else {
            res = { {"status", "failure"}, {"reason", "Unknown request type"} };
        }
        string responseStr = res.dump() + "\n";
        write(clientSock, responseStr.c_str(), responseStr.size());
    } catch (exception &e) {
        string err = json({ {"status", "failure"}, {"reason", e.what()} }).dump() + "\n";
        write(clientSock, err.c_str(), err.size());
    }
    close(clientSock);
}

int main() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        cerr << "Cannot create socket\n";
        exit(1);
    }
    
    // Add socket option to reuse address
    int opt = 1;
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cerr << "Bank setsockopt failed\n";
        exit(1);
    }
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(BANK_HOST);
    serverAddr.sin_port = htons(BANK_PORT);
    if (::bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Bind failed\n";
        exit(1);
    }
    listen(serverSock, 5);
    cout << "Bank server listening on " << BANK_HOST << ":" << BANK_PORT << "\n";
    cout << "Available banks and branches:\n";
    for (const auto& bank : banks) {
        cout << bank.name << " Bank:\n";
        for (const auto& branch : bank.branches) {
            cout << "  - " << branch.name << " (IFSC: " << branch.ifsc << ")\n";
        }
    }
    
    // Create or clear transactions log file
    ofstream logFile(TRANSACTIONS_LOG_FILE);
    if (logFile.is_open()) {
        logFile << "Timestamp | Transaction ID | From | To | Amount" << endl;
        logFile << "------------------------------------------------" << endl;
        logFile.close();
        cout << "Transaction log file created: " << TRANSACTIONS_LOG_FILE << endl;
    }

    while (true) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &addrLen);
        thread(handleClient, clientSock).detach();
    }
    close(serverSock);
    return 0;
}

