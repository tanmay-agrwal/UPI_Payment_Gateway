#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <map>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std;

const int UPI_PORT = 9001;
const string UPI_HOST = "192.168.137.179";

const int BANK_PORT = 9000;
const string BANK_HOST = "192.168.137.153";

const int MERCHANT_NOTIFY_PORT = 9100;

const unsigned long long SECRET_KEY = 0x1234567890ABCDEF;

map<string, string> vmidMapping;

string speck_encrypt(const string &plaintext, unsigned long long key) {
    try {
        unsigned long long plainVal = stoull(plaintext, nullptr, 16);
        unsigned long long cipherVal = plainVal ^ key;
        stringstream ss;
        ss << hex << setw(16) << setfill('0') << cipherVal;
        return ss.str();
    } catch (const std::exception& e) {
        cerr << "Encryption error: " << e.what() << endl;
        return "";
    }
}

string speck_decrypt(const string &ciphertext, unsigned long long key) {
    try {
        unsigned long long cipherVal = stoull(ciphertext, nullptr, 16);
        unsigned long long plainVal = cipherVal ^ key;
        stringstream ss;
        ss << hex << setw(16) << setfill('0') << plainVal;
        return ss.str();
    } catch (const std::exception& e) {
        cerr << "Decryption error: " << e.what() << endl;
        return "";
    }
}

json forwardToBank(const json &request) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        return { {"status", "failure"}, {"reason", "Socket creation error"} };
    }
    sockaddr_in bankAddr;
    bankAddr.sin_family = AF_INET;
    bankAddr.sin_port = htons(BANK_PORT);
    bankAddr.sin_addr.s_addr = inet_addr(BANK_HOST.c_str());
    if(connect(sock, (sockaddr*)&bankAddr, sizeof(bankAddr)) < 0) {
        close(sock);
        return { {"status", "failure"}, {"reason", "Connection to Bank failed"} };
    }
    string reqStr = request.dump() + "\n";
    send(sock, reqStr.c_str(), reqStr.size(), 0);
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    int bytes = read(sock, buffer, sizeof(buffer)-1);
    close(sock);
    if(bytes <= 0)
        return { {"status", "failure"}, {"reason", "No response from Bank"} };
    return json::parse(string(buffer));
}

void notifyMerchant(const json &notification) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
        return;
    sockaddr_in merchantAddr;
    merchantAddr.sin_family = AF_INET;
    merchantAddr.sin_port = htons(MERCHANT_NOTIFY_PORT);
    merchantAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if(connect(sock, (sockaddr*)&merchantAddr, sizeof(merchantAddr)) < 0) {
        close(sock);
        return;
    }
    string msg = notification.dump() + "\n";
    send(sock, msg.c_str(), msg.size(), 0);
    close(sock);
}

void handleClient(int clientSock) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    int bytes = read(clientSock, buffer, sizeof(buffer)-1);
    if(bytes <= 0) {
        close(clientSock);
        return;
    }
    try {
        json req = json::parse(string(buffer));
        string type = req["type"];
        json res;
        if (type == "generate_qr") {
            string mid = req["mid"];
            string vmid = speck_encrypt(mid, SECRET_KEY);
            vmidMapping[vmid] = mid;
            res = { {"status", "success"}, {"vmid", vmid} };
            cout << "Generated VMID for MID " << mid << ": " << vmid << "\n";
        } else if (type == "payment_request") {
            string vmid = req["vmid"];
            string mid;
            if (vmidMapping.find(vmid) != vmidMapping.end()) {
                mid = vmidMapping[vmid];
            } else {
                mid = speck_decrypt(vmid, SECRET_KEY);
                if (mid.empty()) {
                    res = { {"status", "failure"}, {"reason", "Invalid VMID format"} };
                    string resp = res.dump() + "\n";
                    send(clientSock, resp.c_str(), resp.size(), 0);
                    close(clientSock);
                    return;
                }
            }
            json userCheckReq = { {"type", "check_user"}, {"mmid", req["mmid"]} };
            json userCheckRes = forwardToBank(userCheckReq);
            if(userCheckRes["status"] != "success") {
                res = { {"status", "failure"}, {"reason", "User verification failed"} };
                string resp = res.dump() + "\n";
                send(clientSock, resp.c_str(), resp.size(), 0);
                close(clientSock);
                return;
            }
            double userBalance = userCheckRes["user"]["balance"].get<double>();
            double amount = req["amount"].get<double>();
            if(userBalance < amount) {
                res = { {"status", "failure"}, {"reason", "Insufficient funds in user's account"} };
                string resp = res.dump() + "\n";
                send(clientSock, resp.c_str(), resp.size(), 0);
                close(clientSock);
                return;
            }
            json merchantCheckReq = { {"type", "check_merchant"}, {"mid", mid} };
            json merchantCheckRes = forwardToBank(merchantCheckReq);
            if(merchantCheckRes["status"] != "success") {
                res = { {"status", "failure"}, {"reason", "Merchant verification failed"} };
                string resp = res.dump() + "\n";
                send(clientSock, resp.c_str(), resp.size(), 0);
                close(clientSock);
                return;
            }
            json transactionReq = {
                {"type", "transaction"},
                {"mmid", req["mmid"]},
                {"transaction_pin", req["transaction_pin"]},
                {"mid", mid},
                {"amount", req["amount"]}
            };
            json bankRes = forwardToBank(transactionReq);
            res = bankRes;
            notifyMerchant(bankRes);
            cout << "Processed payment. Transaction result: " << bankRes.dump() << "\n";
        } else {
            res = { {"status", "failure"}, {"reason", "Unknown request type"} };
        }
        string responseStr = res.dump() + "\n";
        send(clientSock, responseStr.c_str(), responseStr.size(), 0);
    } catch (exception &e) {
        json err = { {"status", "failure"}, {"reason", e.what()} };
        string errStr = err.dump() + "\n";
        send(clientSock, errStr.c_str(), errStr.size(), 0);
    }
    close(clientSock);
}

int main() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSock < 0) {
        cerr << "Cannot create UPI Machine socket.\n";
        exit(1);
    }
    int opt = 1;
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cerr << "UPI Machine setsockopt failed.\n";
        exit(1);
    }
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(UPI_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(UPI_HOST.c_str());
    if(::bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "UPI Machine bind failed.\n";
        exit(1);
    }
    listen(serverSock, 5);
    cout << "UPI Machine listening on " << UPI_HOST << ":" << UPI_PORT << "\n";
    while(true) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &addrLen);
        thread(handleClient, clientSock).detach();
    }
    close(serverSock);
    return 0;
}
