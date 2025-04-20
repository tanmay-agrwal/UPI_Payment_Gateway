// merchant.cpp
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>
#include <functional>  
#include <stdexcept>   
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <random>      
#include <ctime>       
#include <algorithm>   
#include <cctype>      
#include <openssl/sha.h> 
#include <iomanip>     
#include <fstream>
#include "nlohmann/json.hpp"
#include "qr_utils.h" 

using json = nlohmann::json;
using namespace std;

const int BANK_PORT = 9000;
const string BANK_HOST = "192.168.137.153";

const int UPI_PORT = 9001;
const string UPI_HOST = "192.168.137.179";


const int MERCHANT_NOTIFY_PORT = 9100;


const string QR_IMAGE_FILE = "merchant_payment_qr.png";
const string QR_CONTENT_FILE = "qr_content.txt";


string computeSHA256(const string &input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)input.c_str(), input.size(), hash);
    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    string fullHash = ss.str();
    return fullHash.substr(0, 16); 
}


string generateMerchantID(const string& name, const string& password) {
    
    time_t now = time(nullptr);
    string timeStr = to_string(now);
    
    
    string combined = name + timeStr + password;
    
    
    return computeSHA256(combined);
}


json sendRequest(const string &host, int port, const json &req) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        return { {"status", "failure"}, {"reason", "Socket creation error"} };
    }
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(host.c_str());
    
    if(connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return { {"status", "failure"}, {"reason", "Connection failed"} };
    }
    string reqStr = req.dump() + "\n";
    send(sock, reqStr.c_str(), reqStr.size(), 0);
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    int bytes = read(sock, buffer, sizeof(buffer)-1);
    close(sock);
    if(bytes <= 0)
        return { {"status", "failure"}, {"reason", "No response"} };
    return json::parse(string(buffer));
}


void notificationListener() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSock < 0) {
        cerr << "Merchant notification socket error.\n";
        return;
    }
    
    
    int opt = 1;
    if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cerr << "Merchant notification setsockopt failed.\n";
        return;
    }
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(MERCHANT_NOTIFY_PORT);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if(::bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Merchant notification bind failed.\n";
        return;
    }
    listen(serverSock, 5);
    cout << "Listening for payment confirmations on port " << MERCHANT_NOTIFY_PORT << "\n";
    while(true) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &addrLen);
        char buffer[4096];
        memset(buffer, 0, sizeof(buffer));
        int bytes = read(clientSock, buffer, sizeof(buffer)-1);
        if(bytes > 0) {
            try {
                json notif = json::parse(string(buffer));
                cout << "Payment Notification: " << notif.dump() << "\n";
            } catch(...) {
                cout << "Received invalid notification.\n";
            }
        }
        close(clientSock);
    }
}

int main() {
    
    thread(notificationListener).detach();
    
    string choice;
    string myMid; 
    string myPassword; 
    bool isLoggedIn = false;
    
    while(true) {
        if (!isLoggedIn) {
            cout << "\nMerchant Options:\n";
            cout << "1. Register\n";
            cout << "2. Login\n";
            cout << "3. Exit\n";
            cout << "Enter your choice: ";
            getline(cin, choice);
            
            if(choice == "1") {
                string name, ifsc, password;
                double balance;
                
                cout << "Enter name: ";
                getline(cin, name);
                
                cout << "Enter IFSC code: ";
                getline(cin, ifsc);
                
                cout << "Enter password: ";
                getline(cin, password);
                myPassword = password;
                
                cout << "Enter initial balance: ";
                string balanceStr;
                getline(cin, balanceStr);
                try {
                    balance = stod(balanceStr);
                } catch (const exception& e) {
                    cout << "Invalid balance format. Please enter a number.\n";
                    continue;
                }
                
                
                string merchantId = generateMerchantID(name, password);
                
                json req;
                req["type"] = "register_merchant";
                req["name"] = name;
                req["ifsc"] = ifsc;
                req["password"] = password;
                req["initial_balance"] = balance;
                req["merchant_id"] = merchantId;
                
                json res = sendRequest(BANK_HOST, BANK_PORT, req);
                if(res["status"] == "success") {
                    myMid = res["mid"];
                    isLoggedIn = true;
                    cout << "Registration successful!\n";
                    cout << "Your Merchant ID (MID) is: " << myMid << "\n";
                    cout << "Please save this MID for future logins.\n";
                } else {
                    cout << "Registration failed: " << res["reason"] << "\n";
                }
            }
            else if(choice == "2") {
                cout << "Enter your Merchant ID: ";
                getline(cin, myMid);
                
                cout << "Enter password: ";
                getline(cin, myPassword);
                
                json req;
                req["type"] = "login_merchant";
                req["merchant_id"] = myMid;
                req["password"] = myPassword;
                
                json res = sendRequest(BANK_HOST, BANK_PORT, req);
                if(res["status"] == "success") {
                    isLoggedIn = true;
                    myMid = res["mid"];
                    cout << "Login successful!\n";
                    cout << "Your current balance is: Rs. " << fixed << setprecision(2) << res["balance"] << "\n";
                } else {
                    cout << "Login failed: " << res["reason"] << "\n";
                }
            }
            else if(choice == "3") {
                break;
            }
            else {
                cout << "Invalid option. Try again.\n";
            }
        }
        else {
            
            cout << "\nMerchant Options (Logged in as: " << myMid << "):\n";
            cout << "1. Generate Payment QR\n";
            cout << "2. Check Balance\n";
            cout << "3. Logout\n";
            cout << "4. Exit\n";
            cout << "Enter your choice: ";
            getline(cin, choice);
            
            if(choice == "1") {
                json req;
                req["type"] = "generate_qr";
                req["mid"] = myMid;
                
                json res = sendRequest(UPI_HOST, UPI_PORT, req);
                if(res["status"] == "success") {
                    string vmid = res["vmid"];
                    cout << "QR Code (VMID) generated: " << vmid << "\n";
                    
                    if (qr_utils::generate_qr_code(vmid, QR_IMAGE_FILE)) {
                        cout << "QR code image generated: " << QR_IMAGE_FILE << "\n";
                        cout << "Displaying QR code in terminal:\n";
                        qr_utils::display_qr_terminal(vmid);
                        
                        if (qr_utils::show_qr_code(QR_IMAGE_FILE)) {
                            cout << "QR code image opened in viewer.\n";
                        }
                        
                        ofstream qrFile(QR_CONTENT_FILE);
                        if (qrFile.is_open()) {
                            qrFile << vmid;
                            qrFile.close();
                            cout << "QR content saved to " << QR_CONTENT_FILE << "\n";
                        }
                    }
                    
                    cout << "Share this QR code with the user to receive payment.\n";
                } else {
                    cout << "Failed to generate QR code: " << res["reason"] << "\n";
                }
            }
            else if(choice == "2") {
                json req;
                req["type"] = "get_balance_merchant";
                req["merchant_id"] = myMid;
                req["password"] = myPassword;
                
                json res = sendRequest(BANK_HOST, BANK_PORT, req);
                if(res["status"] == "success") {
                    cout << "Current balance: Rs. " << fixed << setprecision(2) << res["balance"] << "\n";
                } else {
                    cout << "Failed to get balance: " << res["reason"] << "\n";
                }
            }
            else if(choice == "3") {
                
                isLoggedIn = false;
                myMid = "";
                myPassword = "";
                cout << "Logged out successfully.\n";
            }
            else if(choice == "4") {
                break;
            }
            else {
                cout << "Invalid option. Try again.\n";
            }
        }
    }
    
    return 0;
}

