#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>  
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/sha.h> 
#include <iomanip>     
#include "third_party/nlohmann/json.hpp"
#include <regex>
#include <cctype>
#include <random>  
#include <ctime>   
#include "qr_utils.h" 

using json = nlohmann::json;
using namespace std;

const int BANK_PORT = 9000;
const string BANK_HOST = "192.168.137.153";  

const int UPI_PORT = 9001;
const string UPI_HOST = "192.168.137.179";  

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

string generateUserID(const string& name, const string& password) {
    
    time_t now = time(nullptr);
    string timeStr = to_string(now);
    
    string combined = name + timeStr + password;
    
    return computeSHA256(combined);
}

string generateMMID(const string& uid, const string& mobile) {
    
    string combined = uid + mobile;
    
    return computeSHA256(combined);
}

bool isStrongPassword(const string& password) {
    
    regex pattern("^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[@$!%*?&])[A-Za-z\\d@$!%*?&]{8,}$");
    return regex_match(password, pattern);
}

bool isValidTransactionPIN(const string& pin) {
    
    return pin.length() == 6 && all_of(pin.begin(), pin.end(), ::isdigit);
}

string getHiddenInput(const string& prompt) {
    string input;
    cout << prompt;
    system("stty -echo");  
    getline(cin, input);
    system("stty echo");   
    cout << endl;
    return input;
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

string scanQRCode() {
    cout << "Scanning QR code...\n";
    try {
        string vmid = qr_utils::scan_qr_code_simulation(QR_CONTENT_FILE);
        cout << "QR code scanned successfully!\n";
        return vmid;
    } catch (const std::exception& e) {
        cout << "Error scanning QR code: " << e.what() << "\n";
        cout << "Please enter the VMID manually: ";
        string vmid;
        getline(cin, vmid);
        return vmid;
    }
}

int main() {
    string choice;
    string myMMID; 
    string myUID; 
    string myPassword;  
    bool isLoggedIn = false;
    
    while(true) {
        if (!isLoggedIn) {
            cout << "\nUser Options:\n";
            cout << "1. Register\n";
            cout << "2. Login\n";
            cout << "3. Exit\n";
            cout << "Enter your choice: ";
            getline(cin, choice);
            
            if(choice == "1") {
                json req;
                req["type"] = "register_user";
                string input;
                
                cout << "Enter name: ";
                getline(cin, input);
                string name = input;
                req["name"] = name;
                
                cout << "Enter mobile number: ";
                getline(cin, input);
                req["mobile"] = input;
                
                string password;
                do {
                    password = getHiddenInput("Enter a strong password (min 8 chars, 1 uppercase, 1 lowercase, 1 number, 1 special char): ");
                    if (!isStrongPassword(password)) {
                        cout << "Password is not strong enough. Please try again.\n";
                    }
                } while (!isStrongPassword(password));
                req["password"] = password;
                
                string transactionPIN;
                do {
                    transactionPIN = getHiddenInput("Enter a 6-digit transaction PIN: ");
                    if (!isValidTransactionPIN(transactionPIN)) {
                        cout << "Transaction PIN must be exactly 6 digits. Please try again.\n";
                    }
                } while (!isValidTransactionPIN(transactionPIN));
                req["transaction_pin"] = transactionPIN;
                
                cout << "Enter IFSC code: ";
                getline(cin, input);
                req["ifsc"] = input;
                
                cout << "Enter initial balance: ";
                getline(cin, input);
                try {
                    req["initial_balance"] = stod(input);
                } catch (const std::invalid_argument& e) {
                    cout << "Invalid balance format. Please enter a number.\n";
                    continue;
                } catch (const std::out_of_range& e) {
                    cout << "Balance value out of range.\n";
                    continue;
                }
                
                string uid = generateUserID(name, password);
                req["user_id"] = uid;  
                
                string mmid = generateMMID(uid, req["mobile"]);
                req["mmid"] = mmid;
                
                json res = sendRequest(BANK_HOST, BANK_PORT, req);
                if(res["status"] == "success") {
                    myMMID = mmid;
                    myUID = uid;
                    myPassword = password; 
                    isLoggedIn = true;
                    cout << "Registered successfully.\n";
                    cout << "Your User ID (UID) is: " << myUID << "\n";
                    cout << "Your Mobile Money ID (MMID) is: " << myMMID << "\n";
                } else {
                    cout << "Registration failed: " << res.dump() << "\n";
                }
            }
            else if(choice == "2") {
                string input;
                cout << "Enter your User ID: ";
                getline(cin, input);
                string uid = input;
                myUID = uid;
                
                string password = getHiddenInput("Enter your password: ");
                
                json req;
                req["type"] = "login_user";
                req["user_id"] = uid;  
                req["password"] = password;
                
                json res = sendRequest(BANK_HOST, BANK_PORT, req);
                if(res["status"] == "success") {
                    myMMID = res["mmid"];
                    myPassword = password; 
                    isLoggedIn = true;
                    cout << "Login successful. Your current balance is: " << res["balance"] << "\n";
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
            cout << "\nUser Options (Logged in as User ID: " << myUID << "):\n";
            cout << "1. Make Payment\n";
            cout << "2. Check Balance\n";
            cout << "3. Logout\n";
            cout << "4. Exit\n";
            cout << "Enter your choice: ";
            getline(cin, choice);
            
            if(choice == "1") {
                json req;
                req["type"] = "payment_request";
                string input;
                
                cout << "Do you want to scan a QR code? (y/n): ";
                getline(cin, input);
                
                string vmid;
                if (input == "y" || input == "Y") {
                    vmid = scanQRCode();
                } else {
                    cout << "Enter the VMID from the merchant: ";
                    getline(cin, vmid);
                }
                req["vmid"] = vmid;
                
                req["mmid"] = myMMID;
                req["user_id"] = myUID;  
                
                cout << "Enter transaction amount: ";
                getline(cin, input);
                try {
                    req["amount"] = stod(input);
                } catch (const std::invalid_argument& e) {
                    cout << "Invalid amount format. Please enter a number.\n";
                    continue;
                } catch (const std::out_of_range& e) {
                    cout << "Amount value out of range.\n";
                    continue;
                }
                
                string transactionPIN = getHiddenInput("Enter your 6-digit transaction PIN: ");
                if (!isValidTransactionPIN(transactionPIN)) {
                    cout << "Invalid transaction PIN format. Transaction cancelled.\n";
                    continue;
                }
                req["transaction_pin"] = transactionPIN;
                
                json res = sendRequest(UPI_HOST, UPI_PORT, req);
                
                if (res["status"] == "success") {
                    cout << "Payment successful!\n";
                    
                    json displayRes = res;
                    if (displayRes.contains("transaction") && displayRes["transaction"].contains("mid")) {
                        displayRes["transaction"]["mid"] = "********"; 
                    }
                    
                    cout << "Transaction details: " << displayRes.dump() << "\n";
                } else {
                    cout << "Payment failed: " << res["reason"] << "\n";
                }
            }
            else if(choice == "2") {
                json req;
                req["type"] = "get_balance_user";
                req["user_id"] = myUID;  
                req["password"] = myPassword;
                json res = sendRequest(BANK_HOST, BANK_PORT, req);
                if(res["status"] == "success") {
                    cout << "Your current balance is: " << res["balance"] << "\n";
                } else {
                    cout << "Error: " << res.dump() << "\n";
                }
            }
            else if(choice == "3") {
                isLoggedIn = false;
                myMMID = "";
                myUID = "";
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
