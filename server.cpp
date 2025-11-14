    // server_oop.cpp - Object Oriented Version
    #include <iostream>
    #include <fstream>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <vector>
    #include <string>
    #include <direct.h>
    #include <cstdio>
    #include <windows.h>
    #include <sstream>
    #include <algorithm>
    #include <cctype>
    #include <memory>
    #include <map>
    #pragma comment(lib, "ws2_32.lib")

    using namespace std;

    class SessionManager {
    private:
        map<SOCKET, bool> authenticatedSessions;
        string valid_username = "admin";
        string valid_password = "password123";

    public:
        bool authenticate(const string& username, const string& password) {
            return (username == valid_username && password == valid_password);
        }

        void addAuthenticatedSession(SOCKET sock) {
            authenticatedSessions[sock] = true;
        }

        void removeSession(SOCKET sock) {
            authenticatedSessions.erase(sock);
        }

        bool isAuthenticated(SOCKET sock) {
            return authenticatedSessions.find(sock) != authenticatedSessions.end();
        }

        string getValidUsername() const { return valid_username; }
        string getValidPassword() const { return valid_password; }
    };

    class FileManager {
    private:
        string uploadFolder;
        string trashFolder;
        string wwwFolder;
        SessionManager& sessionManager;

    public:
        FileManager(SessionManager& sm, const string& upload = "uploads\\", 
                    const string& trash = "trash\\", 
                    const string& www = "www\\")
            : sessionManager(sm), uploadFolder(upload), trashFolder(trash), wwwFolder(www) {
            createDirectories();
        }

        void createDirectories() const {
            _mkdir(uploadFolder.c_str());
            _mkdir(trashFolder.c_str());
            _mkdir(wwwFolder.c_str());
        }

        bool fileExists(const string& filename) const {
            ifstream f(filename, ios::binary);
            return f.good();
        }

        bool moveToTrash(const string& filename) const {
            string sourcePath = uploadFolder + filename;
            string trashPath = trashFolder + filename;

            if (!fileExists(sourcePath)) return false;

            if (fileExists(trashPath)) {
                remove(trashPath.c_str());
            }

            return rename(sourcePath.c_str(), trashPath.c_str()) == 0;
        }

        bool authenticateClient(SOCKET sock) {
            char buf[1024];
            int r = recv(sock, buf, sizeof(buf)-1, 0);
            if (r <= 0) return false;
            buf[r] = '\0';

            string credentials(buf);
            istringstream iss(credentials);
            string username, password;
            iss >> username >> password;

            if (sessionManager.authenticate(username, password)) {
                sessionManager.addAuthenticatedSession(sock);
                return true;
            }
            return false;
        }

        string listFilesInFolder(const string& folder) const {
            string fileList;
            string pattern = folder + "*";
            WIN32_FIND_DATAA ffd;
            HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
            
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    string name = ffd.cFileName;
                    if (name == "." || name == "..") continue;
                    if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        fileList += name + "\n";
                    }
                } while (FindNextFileA(hFind, &ffd));
                FindClose(hFind);
            }
            
            return fileList.empty() ? "(none)\n" : fileList;
        }

        string getUploadFolder() const { return uploadFolder; }
        string getTrashFolder() const { return trashFolder; }
        string getWwwFolder() const { return wwwFolder; }
        SessionManager& getSessionManager() const { return sessionManager; }
    };

    class NetworkManager {
    private:
        static const int BUFFER_SIZE = 8192;

    public:
        static int sendAll(SOCKET sock, const char* data, int len) {
            int total = 0;
            while (total < len) {
                int sent = send(sock, data + total, len - total, 0);
                if (sent == SOCKET_ERROR) return SOCKET_ERROR;
                total += sent;
            }
            return total;
        }

        static int recvAll(SOCKET sock, char* buffer, int expected) {
            int total = 0;
            while (total < expected) {
                int r = recv(sock, buffer + total, expected - total, 0);
                if (r <= 0) return total;
                total += r;
            }
            return total;
        }

        static string recvUntilHeadersEnd(SOCKET sock) {
            string acc;
            char buf[1024];
            fd_set readSet;
            timeval tv;
            
            while (true) {
                FD_ZERO(&readSet);
                FD_SET(sock, &readSet);
                tv.tv_sec = 1;
                tv.tv_usec = 0;
                
                int sel = select(0, &readSet, NULL, NULL, &tv);
                if (sel > 0) {
                    int r = recv(sock, buf, sizeof(buf), 0);
                    if (r <= 0) break;
                    acc.append(buf, buf + r);
                    
                    if (acc.find("\r\n\r\n") != string::npos) {
                        break;
                    }
                    if (acc.size() > 4 && (acc.find("HTTP/") != string::npos || 
                                        acc.find("GET ") == 0 || 
                                        acc.find("POST ") == 0)) {
                        continue;
                    }
                } else if (sel == 0) {
                    if (!acc.empty() && (acc.find("HTTP/") != string::npos || 
                                        acc.find("GET ") == 0 || 
                                        acc.find("POST ") == 0)) {
                        break;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
                
                if (acc.size() > 64 * 1024) break;
            }
            return acc;
        }
    };

    class HttpRequestHandler {
    private:
        FileManager& fileManager;
        NetworkManager& networkManager;

        string urlDecode(const string& str) const {
            string res;
            for (size_t i = 0; i < str.size(); ++i) {
                if (str[i] == '%') {
                    if (i + 2 < str.size()) {
                        int hi = isdigit(str[i+1]) ? str[i+1] - '0' : tolower(str[i+1]) - 'a' + 10;
                        int lo = isdigit(str[i+2]) ? str[i+2] - '0' : tolower(str[i+2]) - 'a' + 10;
                        res += (char)(hi * 16 + lo);
                        i += 2;
                    } else {
                        res += str[i];
                    }
                } else if (str[i] == '+') {
                    res += ' ';
                } else {
                    res += str[i];
                }
            }
            return res;
        }

        string getHeaderValue(const string& headers, const string& key) const {
            size_t pos = headers.find(key);
            if (pos == string::npos) return "";
            size_t lineEnd = headers.find("\r\n", pos);
            if (lineEnd == string::npos) return "";
            size_t colon = headers.find(":", pos);
            if (colon == string::npos || colon > lineEnd) return "";
            size_t valueStart = colon + 1;
            string val = headers.substr(valueStart, lineEnd - valueStart);
            size_t a = val.find_first_not_of(" \t");
            size_t b = val.find_last_not_of(" \t");
            if (a == string::npos) return "";
            return val.substr(a, b - a + 1);
        }

        string getCookieValue(const string& headers, const string& cookieName) const {
            size_t pos = headers.find("Cookie:");
            if (pos == string::npos) return "";
            
            size_t lineEnd = headers.find("\r\n", pos);
            if (lineEnd == string::npos) return "";
            
            string cookieLine = headers.substr(pos, lineEnd - pos);
            size_t namePos = cookieLine.find(cookieName + "=");
            if (namePos == string::npos) return "";
            
            size_t valueStart = namePos + cookieName.length() + 1;
            size_t valueEnd = cookieLine.find(";", valueStart);
            if (valueEnd == string::npos) valueEnd = lineEnd - pos;
            
            return cookieLine.substr(valueStart, valueEnd - valueStart);
        }

        void sendHttpResponse(SOCKET clientSocket, int status, const string& contentType, const string& body, const string& additionalHeaders = "") const {
            string statusText = (status == 200) ? "OK" : 
                            (status == 400) ? "Bad Request" :
                            (status == 401) ? "Unauthorized" :
                            (status == 404) ? "Not Found" :
                            (status == 500) ? "Internal Server Error" : "Unknown";
            
            string resp = "HTTP/1.1 " + to_string(status) + " " + statusText + "\r\n" +
                        "Content-Type: " + contentType + "\r\n" +
                        "Content-Length: " + to_string(body.size()) + "\r\n" +
                        additionalHeaders + "\r\n" + body;
            
            NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
        }

        bool isAuthenticated(const string& headers) const {
            string sessionToken = getCookieValue(headers, "session");
            return sessionToken == "authenticated";
        }

    public:
        HttpRequestHandler(FileManager& fm, NetworkManager& nm) 
            : fileManager(fm), networkManager(nm) {}

        void handleRequest(SOCKET clientSocket, const string& req) {
            size_t pos = req.find("\r\n");
            string requestLine = (pos == string::npos) ? req : req.substr(0, pos);

            string method, path;
            {
                istringstream iss(requestLine);
                iss >> method >> path;
            }

            // Handle login page and login request without authentication
            if (path == "/login" || path == "/login.html") {
                serveLoginPage(clientSocket);
                return;
            }

            if (path == "/auth" && method == "POST") {
                handleLogin(clientSocket, req);
                return;
            }

            if (path == "/logout") {
                handleLogout(clientSocket);
                return;
            }

            if (path == "/") {
                if (isAuthenticated(req)) {
                    serveStaticFile(clientSocket, "/index.html");
                } else {
                    sendHttpResponse(clientSocket, 302, "text/html", "", "Location: /login\r\n");
                }
                return;
            }

            // Check authentication for other routes
            if (!isAuthenticated(req)) {
                sendHttpResponse(clientSocket, 401, "text/html",
                    "<html><body><h1>401 Unauthorized</h1><p>Please <a href='/login'>login</a></p></body></html>");
                return;
            }

            // Handle authenticated routes
            if (method == "GET") {
                handleGetRequest(clientSocket, path);
            } else if (method == "POST") {
                handlePostRequest(clientSocket, req, path);
            } else {
                sendHttpResponse(clientSocket, 400, "text/plain", "Unsupported request method");
            }
        }

    private:
        void serveLoginPage(SOCKET clientSocket) const {
            string loginPage = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="utf-8">
        <title>FTP Server - Login</title>
        <style>
            * { margin: 0; padding: 0; box-sizing: border-box; }
            body { 
                font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                height: 100vh;
                display: flex;
                align-items: center;
                justify-content: center;
            }
            .login-container {
                background: rgba(255, 255, 255, 0.95);
                padding: 40px;
                border-radius: 15px;
                box-shadow: 0 10px 30px rgba(0, 0, 0, 0.2);
                width: 100%;
                max-width: 400px;
            }
            h1 { 
                text-align: center; 
                margin-bottom: 30px;
                color: #2c3e50;
            }
            .form-group { margin-bottom: 20px; }
            label { display: block; margin-bottom: 5px; color: #34495e; }
            input[type="text"], input[type="password"] {
                width: 100%;
                padding: 12px;
                border: 2px solid #ddd;
                border-radius: 8px;
                font-size: 14px;
            }
            input:focus { outline: none; border-color: #3498db; }
            button {
                width: 100%;
                padding: 12px;
                background: #3498db;
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 16px;
                cursor: pointer;
                transition: background 0.3s ease;
            }
            button:hover { background: #2980b9; }
            .error { 
                color: #e74c3c; 
                margin-top: 10px; 
                text-align: center;
                min-height: 20px;
            }
            .credentials {
                margin-top: 20px;
                padding: 15px;
                background: #f8f9fa;
                border-radius: 8px;
                border-left: 4px solid #3498db;
            }
            .credentials h3 {
                margin-bottom: 10px;
                color: #2c3e50;
            }
        </style>
    </head>
    <body>
        <div class="login-container">
            <h1>🔐 FTP Server Login</h1>
            <form id="loginForm">
                <div class="form-group">
                    <label>Username:</label>
                    <input type="text" id="username" name="username" required>
                </div>
                <div class="form-group">
                    <label>Password:</label>
                    <input type="password" id="password" name="password" required>
                </div>
                <button type="submit">Login</button>
            </form>
            <div id="error" class="error"></div>
            
            <div class="credentials">
                <h3>Default Credentials:</h3>
                <p><strong>Username:</strong> admin</p>
                <p><strong>Password:</strong> password123</p>
            </div>
        </div>

        <script>
            document.getElementById('loginForm').addEventListener('submit', async (e) => {
                e.preventDefault();
                
                const username = document.getElementById('username').value;
                const password = document.getElementById('password').value;
                const errorDiv = document.getElementById('error');
                
                errorDiv.textContent = ''; // Clear previous errors
                
                try {
                    const formData = new FormData();
                    formData.append('username', username);
                    formData.append('password', password);
                    
                    const response = await fetch('/auth', {
                        method: 'POST',
                        body: formData
                    });
                    
                    if (response.ok) {
                        // Login successful, redirect to main page
                        window.location.href = '/';
                    } else {
                        const errorText = await response.text();
                        errorDiv.textContent = 'Login failed: ' + errorText;
                    }
                } catch (error) {
                    errorDiv.textContent = 'Login failed: ' + error.message;
                }
            });

            // Auto-focus username field
            document.getElementById('username').focus();
        </script>
    </body>
    </html>
            )";
            sendHttpResponse(clientSocket, 200, "text/html", loginPage);
        }

        void handleLogin(SOCKET clientSocket, const string& req) {
        size_t headersEnd = req.find("\r\n\r\n");
        if (headersEnd == string::npos) {
            sendHttpResponse(clientSocket, 400, "text/plain", "Bad Request");
            return;
        }

        string body = req.substr(headersEnd + 4);
        string username, password;

        // Check if it's multipart/form-data
        string contentType = getHeaderValue(req, "Content-Type");
        if (contentType.find("multipart/form-data") != string::npos) {
            // Parse multipart form data
            size_t boundaryPos = contentType.find("boundary=");
            if (boundaryPos != string::npos) {
                size_t start = boundaryPos + 9;
                size_t end = contentType.find(';', start);
                if (end == string::npos) end = contentType.size();
                string b = contentType.substr(start, end - start);
                // trim spaces
                size_t first = b.find_first_not_of(" \t");
                size_t last = b.find_last_not_of(" \t");
                if (first != string::npos) b = b.substr(first, last - first + 1);
                // remove quotes
                if (!b.empty() && b[0] == '"') b = b.substr(1);
                if (!b.empty() && b.back() == '"') b.pop_back();
                string boundary = "--" + b;
                
                // Find username field
                size_t userPos = body.find(boundary);
                if (userPos != string::npos) {
                    userPos = body.find("name=\"username\"", userPos);
                    if (userPos != string::npos) {
                        userPos = body.find("\r\n\r\n", userPos);
                        if (userPos != string::npos) {
                            userPos += 4;
                            size_t userEnd = body.find("\r\n" + boundary, userPos);
                            if (userEnd != string::npos) {
                                username = body.substr(userPos, userEnd - userPos);
                                // Remove trailing CRLF
                                while (!username.empty() && (username.back() == '\r' || username.back() == '\n')) {
                                    username.pop_back();
                                }
                            }
                        }
                    }
                }

                // Find password field
                size_t passPos = body.find(boundary, userPos != string::npos ? userPos : 0);
                if (passPos != string::npos) {
                    passPos = body.find("name=\"password\"", passPos);
                    if (passPos != string::npos) {
                        passPos = body.find("\r\n\r\n", passPos);
                        if (passPos != string::npos) {
                            passPos += 4;
                            size_t passEnd = body.find("\r\n" + boundary, passPos);
                            if (passEnd != string::npos) {
                                password = body.substr(passPos, passEnd - passPos);
                                // Remove trailing CRLF
                                while (!password.empty() && (password.back() == '\r' || password.back() == '\n')) {
                                    password.pop_back();
                                }
                            }
                        }
                    }
                }
            }
        } else {
            // Parse URL-encoded form data (original code)
            size_t userPos = body.find("username=");
            size_t passPos = body.find("&password=");
            if (userPos != string::npos && passPos != string::npos) {
                username = urlDecode(body.substr(userPos + 9, passPos - (userPos + 9)));
                password = urlDecode(body.substr(passPos + 10));
            }
        }

        // Debug output
        cout << "Login attempt - Username: " << username << ", Password: " << password << endl;

        if (fileManager.getSessionManager().authenticate(username, password)) {
            string redirectPage = R"(
    <html>
    <head>
        <meta http-equiv="refresh" content="0;url=/">
    </head>
    <body>
        <p>Login successful! Redirecting...</p>
    </body>
    </html>
            )";
            sendHttpResponse(clientSocket, 200, "text/html", redirectPage, 
                        "Set-Cookie: session=authenticated; Path=/; HttpOnly");
            cout << "Login successful for user: " << username << endl;
        } else {
            sendHttpResponse(clientSocket, 401, "text/plain", "Invalid credentials");
            cout << "Login failed for user: " << username << endl;
        }
    }

        void handleLogout(SOCKET clientSocket) {
            string logoutPage = R"(
    <html>
    <head>
        <meta http-equiv="refresh" content="2;url=/login">
    </head>
    <body>
        <p>Logged out successfully. Redirecting to login page...</p>
    </body>
    </html>
            )";
            sendHttpResponse(clientSocket, 200, "text/html", logoutPage,
                        "Set-Cookie: session=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
        }

        void handleGetRequest(SOCKET clientSocket, const string& path) {
            string actualPath = (path == "/") ? "/index.html" : path;

            if (actualPath.rfind("/list_trash", 0) == 0) {
                string list = "=== Trash Files ===\n" + fileManager.listFilesInFolder(fileManager.getTrashFolder());
                sendHttpResponse(clientSocket, 200, "text/plain", list);
                return;
            }
            
            if (actualPath.rfind("/list", 0) == 0) {
                string list = "=== Server Files ===\n" + fileManager.listFilesInFolder(fileManager.getUploadFolder());
                sendHttpResponse(clientSocket, 200, "text/plain", list);
                return;
            }

            if (actualPath.rfind("/download", 0) == 0) {
                handleDownloadRequest(clientSocket, actualPath);
                return;
            }

            serveStaticFile(clientSocket, actualPath);
        }

        void handleDownloadRequest(SOCKET clientSocket, const string& path) {
            size_t q = path.find("?");
            string filename;
            if (q != string::npos) {
                string query = path.substr(q + 1);
                size_t eq = query.find("file=");
                if (eq != string::npos) filename = query.substr(eq + 5);
            }
            
            if (filename.empty()) {
                sendHttpResponse(clientSocket, 400, "text/plain", "Missing file parameter");
                return;
            }

            string filepath = fileManager.getUploadFolder() + filename;
            if (!fileManager.fileExists(filepath)) {
                sendHttpResponse(clientSocket, 404, "text/plain", "File not found");
                return;
            }

            ifstream file(filepath, ios::binary | ios::ate);
            if (!file.is_open()) {
                sendHttpResponse(clientSocket, 500, "text/plain", "Unable to open file");
                return;
            }

            streamsize fileSize = file.tellg();
            file.seekg(0, ios::beg);

            string header = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n" +
                        string("Content-Length: ") + to_string((long long)fileSize) + "\r\n" +
                        "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n\r\n";
            
            NetworkManager::sendAll(clientSocket, header.c_str(), (int)header.size());

            char buf[8192];
            while (file.good()) {
                file.read(buf, sizeof(buf));
                streamsize got = file.gcount();
                if (got > 0) {
                    if (NetworkManager::sendAll(clientSocket, buf, (int)got) == SOCKET_ERROR) break;
                }
            }
            file.close();
        }

        void serveStaticFile(SOCKET clientSocket, const string& path) {
            string localPath = fileManager.getWwwFolder() + path.substr(1);
            
            if (localPath.find("..") != string::npos) {
                sendHttpResponse(clientSocket, 403, "text/plain", "Forbidden");
                return;
            }
            
            if (!fileManager.fileExists(localPath)) {
                sendHttpResponse(clientSocket, 404, "text/plain", "Not Found");
                return;
            }

            string contentType = "application/octet-stream";
            if (localPath.rfind(".html") != string::npos) contentType = "text/html";
            else if (localPath.rfind(".css") != string::npos) contentType = "text/css";
            else if (localPath.rfind(".js") != string::npos) contentType = "application/javascript";
            else if (localPath.rfind(".png") != string::npos) contentType = "image/png";

            ifstream in(localPath, ios::binary | ios::ate);
            streamsize size = in.tellg();
            in.seekg(0, ios::beg);
            
            string header = "HTTP/1.1 200 OK\r\nContent-Type: " + contentType + "\r\n" +
                        "Content-Length: " + to_string((long long)size) + "\r\n\r\n";
            
            NetworkManager::sendAll(clientSocket, header.c_str(), (int)header.size());

            char buf[8192];
            while (in.good()) {
                in.read(buf, sizeof(buf));
                streamsize got = in.gcount();
                if (got > 0) {
                    if (NetworkManager::sendAll(clientSocket, buf, (int)got) == SOCKET_ERROR) break;
                }
            }
            in.close();
        }

        void handlePostRequest(SOCKET clientSocket, const string& req, const string& path) {
            size_t headersEnd = req.find("\r\n\r\n");
            if (headersEnd == string::npos) {
                sendHttpResponse(clientSocket, 400, "text/plain", "Bad Request");
                return;
            }

            string headers = req.substr(0, headersEnd + 4);
            string body = req.substr(headersEnd + 4);
            string contentLengthStr = getHeaderValue(headers, "Content-Length");
            long long contentLength = contentLengthStr.empty() ? 0 : atoll(contentLengthStr.c_str());

            int already = (int)body.size();
            while (already < contentLength) {
                char tmp[8192];
                int r = recv(clientSocket, tmp, sizeof(tmp), 0);
                if (r <= 0) break;
                body.append(tmp, tmp + r);
                already += r;
            }

            size_t q = path.find("?");
            string filename;
            if (q != string::npos) {
                string query = path.substr(q + 1);
                size_t eq = query.find("filename=");
                if (eq != string::npos) filename = urlDecode(query.substr(eq + 9));
            }

            if (path.rfind("/upload", 0) == 0) {
                handleUpload(clientSocket, filename, body);
            } else if (path.rfind("/delete", 0) == 0) {
                handleDelete(clientSocket, filename);
            } else if (path.rfind("/restore", 0) == 0) {
                handleRestore(clientSocket, filename);
            } else if (path.rfind("/delete_permanent", 0) == 0) {
                handleDeletePermanent(clientSocket, filename);
            } else if (path.rfind("/empty_trash", 0) == 0) {
                handleEmptyTrash(clientSocket);
            } else {
                sendHttpResponse(clientSocket, 404, "text/plain", "Unknown POST endpoint");
            }
        }

        void handleUpload(SOCKET clientSocket, const string& filename, const string& body) {
            if (filename.empty()) {
                sendHttpResponse(clientSocket, 400, "text/plain", "Missing filename param");
                return;
            }

            string filepath = fileManager.getUploadFolder() + filename;
            ofstream out(filepath, ios::binary);
            if (!out.is_open()) {
                sendHttpResponse(clientSocket, 500, "text/plain", "Error creating file");
                return;
            }

            out.write(body.data(), body.size());
            out.close();

            string trashPath = fileManager.getTrashFolder() + filename;
            if (fileManager.fileExists(trashPath)) {
                DeleteFileA(trashPath.c_str());
            }

            sendHttpResponse(clientSocket, 200, "text/plain", "File uploaded");
        }

        void handleDelete(SOCKET clientSocket, const string& filename) {
            if (filename.empty()) {
                sendHttpResponse(clientSocket, 400, "text/plain", "Missing filename param");
                return;
            }

            if (fileManager.moveToTrash(filename)) {
                sendHttpResponse(clientSocket, 200, "text/plain", "Moved to trash");
            } else {
                sendHttpResponse(clientSocket, 500, "text/plain", "Error moving file");
            }
        }

        void handleRestore(SOCKET clientSocket, const string& filename) {
            if (filename.empty()) {
                sendHttpResponse(clientSocket, 400, "text/plain", "Missing filename param");
                return;
            }

            string trashPath = fileManager.getTrashFolder() + filename;
            string uploadPath = fileManager.getUploadFolder() + filename;
            
            if (MoveFileExA(trashPath.c_str(), uploadPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
                sendHttpResponse(clientSocket, 200, "text/plain", "Restored");
            } else {
                sendHttpResponse(clientSocket, 500, "text/plain", "Error restoring");
            }
        }

        void handleDeletePermanent(SOCKET clientSocket, const string& filename) {
            if (filename.empty()) {
                sendHttpResponse(clientSocket, 400, "text/plain", "Missing filename param");
                return;
            }

            string trashPath = fileManager.getTrashFolder() + filename;
            if (DeleteFileA(trashPath.c_str())) {
                sendHttpResponse(clientSocket, 200, "text/plain", "Permanently deleted");
            } else {
                sendHttpResponse(clientSocket, 500, "text/plain", "Error deleting file");
            }
        }

        void handleEmptyTrash(SOCKET clientSocket) {
            string pattern = fileManager.getTrashFolder() + "*";
            WIN32_FIND_DATAA ffd;
            HANDLE hFind = FindFirstFileA(pattern.c_str(), &ffd);
            int deletedCount = 0;
            
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    string name = ffd.cFileName;
                    if (name == "." || name == "..") continue;
                    if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        string filepath = fileManager.getTrashFolder() + name;
                        if (DeleteFileA(filepath.c_str())) deletedCount++;
                    }
                } while (FindNextFileA(hFind, &ffd));
                FindClose(hFind);
            }
            
            sendHttpResponse(clientSocket, 200, "text/plain", 
                            "Deleted " + to_string(deletedCount) + " files from trash");
        }
    };

    class CommandHandler {
    private:
        FileManager& fileManager;
        NetworkManager& networkManager;

    public:
        CommandHandler(FileManager& fm, NetworkManager& nm) 
            : fileManager(fm), networkManager(nm) {}

        void handleCommand(SOCKET clientSocket, const string& command) {
            // First message should be authentication
            if (!fileManager.getSessionManager().isAuthenticated(clientSocket)) {
                if (!fileManager.authenticateClient(clientSocket)) {
                    string resp = "AUTH FAILED";
                    NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
                    closesocket(clientSocket);
                    return;
                } else {
                    string resp = "AUTH OK";
                    NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
                    return;
                }
            }

            string cmd = command;
            while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n')) {
                cmd.pop_back();
            }

            cout << "Received command: " << cmd << endl;

            if (cmd.rfind("UPLOAD ", 0) == 0) {
                handleUploadCommand(clientSocket, cmd.substr(7));
            } else if (cmd.rfind("DOWNLOAD ", 0) == 0) {
                handleDownloadCommand(clientSocket, cmd.substr(9));
            } else if (cmd == "LIST") {
                handleListCommand(clientSocket);
            } else if (cmd.rfind("DELETE ", 0) == 0) {
                handleDeleteCommand(clientSocket, cmd.substr(7));
            } else if (cmd == "LIST_TRASH") {
                handleListTrashCommand(clientSocket);
            } else if (cmd.rfind("RESTORE ", 0) == 0) {
                handleRestoreCommand(clientSocket, cmd.substr(8));
            } else {
                string resp = "Unknown command";
                NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
            }
        }

    private:
        void handleUploadCommand(SOCKET clientSocket, const string& filename) {
            string filepath = fileManager.getUploadFolder() + filename;
            string ready = "READY";
            NetworkManager::sendAll(clientSocket, ready.c_str(), (int)ready.size());

            ofstream out(filepath, ios::binary);
            if (!out.is_open()) {
                string resp = "Error creating file";
                NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
                return;
            }

            char buf[8192];
            int r;
            while ((r = recv(clientSocket, buf, sizeof(buf), 0)) > 0) {
                out.write(buf, r);
                if (r < (int)sizeof(buf)) break;
            }
            out.close();

            string resp = "File uploaded: " + filename;
            NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
        }

        void handleDownloadCommand(SOCKET clientSocket, const string& filename) {
            string filepath = fileManager.getUploadFolder() + filename;
            if (!fileManager.fileExists(filepath)) {
                string err = "File not found: " + filename;
                NetworkManager::sendAll(clientSocket, err.c_str(), (int)err.size());
                return;
            }

            ifstream in(filepath, ios::binary | ios::ate);
            streamsize size = in.tellg();
            in.seekg(0, ios::beg);
            char buf[8192];
            
            while (in.good()) {
                in.read(buf, sizeof(buf));
                streamsize g = in.gcount();
                if (g > 0) {
                    if (NetworkManager::sendAll(clientSocket, buf, (int)g) == SOCKET_ERROR) break;
                }
            }
            in.close();
        }

        void handleListCommand(SOCKET clientSocket) {
            string listing = "=== Server Files ===\n" + fileManager.listFilesInFolder(fileManager.getUploadFolder());
            NetworkManager::sendAll(clientSocket, listing.c_str(), (int)listing.size());
        }

        void handleDeleteCommand(SOCKET clientSocket, const string& filename) {
            if (fileManager.moveToTrash(filename)) {
                string resp = "File moved to trash: " + filename;
                NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
            } else {
                string resp = "Error moving file to trash: " + filename;
                NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
            }
        }

        void handleListTrashCommand(SOCKET clientSocket) {
            string listing = "=== Trash Files ===\n" + fileManager.listFilesInFolder(fileManager.getTrashFolder());
            NetworkManager::sendAll(clientSocket, listing.c_str(), (int)listing.size());
        }

        void handleRestoreCommand(SOCKET clientSocket, const string& filename) {
            string trashPath = fileManager.getTrashFolder() + filename;
            string uploadPath = fileManager.getUploadFolder() + filename;
            
            if (MoveFileExA(trashPath.c_str(), uploadPath.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
                string resp = "File restored: " + filename;
                NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
            } else {
                string resp = "Error restoring file: " + filename;
                NetworkManager::sendAll(clientSocket, resp.c_str(), (int)resp.size());
            }
        }
    };

    class FTPServer {
    private:
        SessionManager sessionManager;
        FileManager fileManager;
        NetworkManager networkManager;
        HttpRequestHandler httpHandler;
        CommandHandler commandHandler;
        SOCKET serverSocket;
        bool running;

    public:
        FTPServer() 
            : sessionManager(), fileManager(sessionManager), networkManager(), 
            httpHandler(fileManager, networkManager),
            commandHandler(fileManager, networkManager),
            serverSocket(INVALID_SOCKET), running(false) {}

        ~FTPServer() {
            stop();
        }

        bool start(int port = 8080) {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
                cout << "WSAStartup failed\n";
                return false;
            }

            serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (serverSocket == INVALID_SOCKET) {
                cout << "Socket creation failed\n";
                WSACleanup();
                return false;
            }

            BOOL opt = TRUE;
            setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

            sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(port);
            serverAddr.sin_addr.s_addr = INADDR_ANY;

            if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                cout << "Bind failed. Error: " << WSAGetLastError() << "\n";
                closesocket(serverSocket);
                WSACleanup();
                return false;
            }

            if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
                cout << "Listen failed\n";
                closesocket(serverSocket);
                WSACleanup();
                return false;
            }

            running = true;
            cout << "Server listening on http://localhost:" << port << "/\n";
            cout << "Default credentials: admin / password123\n";
            return true;
        }

        void run() {
            while (running) {
                SOCKET clientSocket = accept(serverSocket, NULL, NULL);
                if (clientSocket == INVALID_SOCKET) {
                    if (running) cout << "Accept failed\n";
                    continue;
                }
                handleClient(clientSocket);
            }
        }

        void stop() {
            running = false;
            if (serverSocket != INVALID_SOCKET) {
                closesocket(serverSocket);
                serverSocket = INVALID_SOCKET;
            }
            WSACleanup();
        }

    private:
        void handleClient(SOCKET clientSocket) {
            char buffer[8192];
            int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            
            if (bytesReceived <= 0) {
                closesocket(clientSocket);
                return;
            }
            
            buffer[bytesReceived] = '\0';
            string request(buffer);
            
            if (request.find("HTTP/") != string::npos || 
                request.find("GET ") == 0 || 
                request.find("POST ") == 0) {
                
                while (request.find("\r\n\r\n") == string::npos) {
                    int more = recv(clientSocket, buffer, sizeof(buffer), 0);
                    if (more <= 0) break;
                    buffer[more] = '\0';
                    request.append(buffer);
                    if (request.size() > 64 * 1024) break;
                }
                
                httpHandler.handleRequest(clientSocket, request);
            } else {
                commandHandler.handleCommand(clientSocket, request);
            }
            
            closesocket(clientSocket);
        }
    };

    int main() {
        FTPServer server;
        
        if (!server.start(8080)) {
            return 1;
        }
        
        server.run();
        return 0;
    }