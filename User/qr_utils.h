// qr_utils.h
#ifndef QR_UTILS_H
#define QR_UTILS_H

#include <string>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <cstdio>
#include <memory>

/**
 * Utility functions for QR code generation and scanning
 * This implementation uses the qrencode library (which must be installed)
 * For MacOS: brew install qrencode
 * For Ubuntu/Debian: sudo apt-get install qrencode
 */

namespace qr_utils {

/**
 * Generate a QR code image for the given data
 * @param data The string data to encode in the QR code
 * @param filename The output file name (PNG)
 * @param size Size of the QR code in pixels (default: 300)
 * @return True if successful, false otherwise
 */
bool generate_qr_code(const std::string& data, const std::string& filename, int size = 300) {
    // Create command: qrencode -o filename.png -s size data
    std::stringstream cmd;
    cmd << "qrencode -o " << filename << " -s " << size/10 << " \"" << data << "\"";
    
    // Execute the command
    int result = system(cmd.str().c_str());
    return (result == 0);
}

/**
 * Display a QR code on the terminal (for testing)
 * @param data The string data to encode in the QR code
 * @return True if successful, false otherwise
 */
bool display_qr_terminal(const std::string& data) {
    // Create command: qrencode -t UTF8 data
    std::stringstream cmd;
    cmd << "qrencode -t UTF8 \"" << data << "\"";
    
    // Execute the command
    int result = system(cmd.str().c_str());
    return (result == 0);
}

/**
 * Open a QR code image with the default image viewer
 * @param filename The QR code image filename
 * @return True if successful, false otherwise
 */
bool show_qr_code(const std::string& filename) {
    // Create command to open the image with the default viewer
    std::stringstream cmd;
    
    #ifdef __APPLE__
    cmd << "open " << filename;
    #elif defined(__linux__)
    cmd << "xdg-open " << filename;
    #else
    return false; // Unsupported platform
    #endif
    
    // Execute the command
    int result = system(cmd.str().c_str());
    return (result == 0);
}

/**
 * Simulate scanning a QR code (for testing purposes)
 * Since this is a simulation, we simply load the data from a file
 * In a real application, this would use a camera library to capture and decode a QR code
 * @param filename The QR code content file
 * @return The scanned data
 */
std::string scan_qr_code_simulation(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open QR code content file: " + filename);
    }
    
    std::string content;
    std::getline(file, content);
    return content;
}

} // namespace qr_utils

#endif // QR_UTILS_H 