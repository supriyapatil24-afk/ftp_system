#include <iostream>
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <direct.h>
#include <memory>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

class NetworkClient {
private:
    string serverHost;
    int serverPort;
    WSADATA wsaData;
    bool initialized;

public:
    NetworkClient(const string& host = "127.0.0.1", int port = 8080) 
        : serverHost(host), serverPort(port), initialized(false) {}

    ~NetworkClient() {
        if (initialized) {
            WSACleanup();
        }
    }

    bool initialize() {
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            cout << "WSAStartup failed\n";
            return false;
        }
        initialized = true;
        return true;
    }
    
    SOCKET connectToServer() const {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            cout << "Socket creation failed. Error: " << WSAGetLastError() << endl;
            return INVALID_SOCKET;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        inet_pton(AF_INET, serverHost.c_str(), &serverAddr.sin_addr);

        if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            cout << "Connection failed. Make sure server is running. Error: " << WSAGetLastError() << endl;
            closesocket(sock);
            return INVALID_SOCKET;
        }

        return sock;
    }
    
    bool authenticate(SOCKET sock) {
        string username, password;
        cout << "Enter username: ";
        cin >> username;
        cout << "Enter password: ";
        cin >> password;
        
        string credentials = username + " " + password;
        send(sock, credentials.c_str(), credentials.size(), 0);
        char buf[1024];
        int r = recv(sock, buf, sizeof(buf)-1, 0);
        if (r <= 0) return false;
        buf[r] = '\0';
        return string(buf).find("AUTH OK") != string::npos;
    }

    static int sendAll(SOCKET sock, const char* data, int len) {
        int total = 0;
        while (total < len) {
            int s = send(sock, data + total, len - total, 0);
            if (s == SOCKET_ERROR) return SOCKET_ERROR;
            total += s;
        }
        return total;
    }

    string getServerHost() const { return serverHost; }
    int getServerPort() const { return serverPort; }
    bool isInitialized() const { return initialized; }
};

class FileSystem {
private:
    string downloadFolder;

public:
    FileSystem(const string& folder = "downloads\\") : downloadFolder(folder) {
        createDirectory(downloadFolder);
    }

    void createDirectory(const string& path) const {
        _mkdir(path.c_str());
    }

    bool fileExists(const string& filename) const {
        ifstream f(filename, ios::binary);
        return f.good();
    }

    string getDownloadFolder() const { return downloadFolder; }
};

class FTPClient {
private:
    NetworkClient networkClient;
    FileSystem fileSystem;

public:
    FTPClient(const string& host = "127.0.0.1", int port = 8080) 
        : networkClient(host, port), fileSystem() {}

    bool initialize() {
        return networkClient.initialize();
    }

    bool uploadFile(const string& filename) {
        SOCKET sock = networkClient.connectToServer();
        if (sock == INVALID_SOCKET) return false;

        if (!networkClient.authenticate(sock)) {
            cout << "Authentication failed!\n";
            closesocket(sock);
            return false;
        }

        ifstream in(filename, ios::binary | ios::ate);
        if (!in.is_open()) {
            cout << "Cannot open file: " << filename << endl;
            closesocket(sock);
            return false;
        }

        streamsize size = in.tellg();
        in.seekg(0, ios::beg);

        string command = "UPLOAD " + filename;
        if (NetworkClient::sendAll(sock, command.c_str(), (int)command.size()) == SOCKET_ERROR) {
            cout << "Error sending command\n";
            in.close();
            closesocket(sock);
            return false;
        }

        char buf[4096];
        int r = recv(sock, buf, sizeof(buf)-1, 0);
        if (r <= 0) {
            cout << "No response from server\n";
            in.close();
            closesocket(sock);
            return false;
        }

        buf[r] = '\0';
        string resp(buf);
        if (resp != "READY") {
            cout << "Server response: " << resp << endl;
            in.close();
            closesocket(sock);
            return false;
        }

        const int CHUNK = 4096;
        vector<char> buffer(CHUNK);
        streamsize totalSent = 0;
        
        while (in.good()) {
            in.read(buffer.data(), CHUNK);
            streamsize got = in.gcount();
            if (got <= 0) break;
            
            int s = NetworkClient::sendAll(sock, buffer.data(), (int)got);
            if (s == SOCKET_ERROR) {
                cout << "Error sending file bytes\n";
                in.close();
                closesocket(sock);
                return false;
            }
            totalSent += s;
        }
        
        in.close();
        cout << "Sent bytes: " << totalSent << endl;

        waitForServerResponse(sock);
        closesocket(sock);
        return true;
    }

    bool downloadFile() {
        string serverFilename;
        cout << "Enter server filename to download: ";
        cin >> serverFilename;

        SOCKET sock = networkClient.connectToServer();
        if (sock == INVALID_SOCKET) return false;

        if (!networkClient.authenticate(sock)) {
            cout << "Authentication failed!\n";
            closesocket(sock);
            return false;
        }

        string local = fileSystem.getDownloadFolder() + serverFilename;

        if (fileSystem.fileExists(local)) {
            char c; 
            cout << "Overwrite " << local << "? (y/n): "; 
            cin >> c;
            if (c != 'y' && c != 'Y') {
                closesocket(sock);
                return false;
            }
        }

        string cmd = "DOWNLOAD " + serverFilename;
        if (NetworkClient::sendAll(sock, cmd.c_str(), (int)cmd.size()) == SOCKET_ERROR) {
            cout << "Error sending command\n";
            closesocket(sock);
            return false;
        }

        ofstream out(local, ios::binary);
        if (!out.is_open()) {
            cout << "Cannot create local file\n";
            closesocket(sock);
            return false;
        }

        receiveFileData(sock, out);
        out.close();
        
        cout << "Downloaded to: " << local << endl;
        closesocket(sock);
        return true;
    }

    void listServerFiles() {
        SOCKET sock = networkClient.connectToServer();
        if (sock == INVALID_SOCKET) return;

        if (!networkClient.authenticate(sock)) {
            cout << "Authentication failed!\n";
            closesocket(sock);
            return;
        }

        NetworkClient::sendAll(sock, "LIST", 4);

        char buf[4096];
        string acc;
        int r;
        
        while ((r = recv(sock, buf, sizeof(buf), 0)) > 0) {
            acc.append(buf, buf + r);
            if (r < (int)sizeof(buf)) break;
        }
        
        cout << acc << endl;
        closesocket(sock);
    }

    void listTrashFiles() {
        SOCKET sock = networkClient.connectToServer();
        if (sock == INVALID_SOCKET) return;

        if (!networkClient.authenticate(sock)) {
            cout << "Authentication failed!\n";
            closesocket(sock);
            return;
        }

        NetworkClient::sendAll(sock, "LIST_TRASH", 10);
        
        char buf[4096];
        string acc;
        int r;
        
        while ((r = recv(sock, buf, sizeof(buf), 0)) > 0) {
            acc.append(buf, buf + r);
            if (r < (int)sizeof(buf)) break;
        }
        
        cout << acc << endl;
        closesocket(sock);
    }

    void deleteServerFile() {
        string filename;
        cout << "Enter filename to delete: ";
        cin >> filename;

        SOCKET sock = networkClient.connectToServer();
        if (sock == INVALID_SOCKET) return;

        if (!networkClient.authenticate(sock)) {
            cout << "Authentication failed!\n";
            closesocket(sock);
            return;
        }

        string cmd = "DELETE " + filename;
        NetworkClient::sendAll(sock, cmd.c_str(), (int)cmd.size());

        char buf[4096];
        string acc;
        int r;
        
        while ((r = recv(sock, buf, sizeof(buf), 0)) > 0) {
            acc.append(buf, buf + r);
            if (r < (int)sizeof(buf)) break;
        }
        
        cout << "Server: " << acc << endl;
        closesocket(sock);
    }

    void restoreFromTrash() {
        string filename;
        cout << "Enter filename to restore: ";
        cin >> filename;

        SOCKET sock = networkClient.connectToServer();
        if (sock == INVALID_SOCKET) return;

        if (!networkClient.authenticate(sock)) {
            cout << "Authentication failed!\n";
            closesocket(sock);
            return;
        }

        string cmd = "RESTORE " + filename;
        NetworkClient::sendAll(sock, cmd.c_str(), (int)cmd.size());

        char buf[4096];
        string acc;
        int r;
        
        while ((r = recv(sock, buf, sizeof(buf), 0)) > 0) {
            acc.append(buf, buf + r);
            if (r < (int)sizeof(buf)) break;
        }
        
        cout << "Server: " << acc << endl;
        closesocket(sock);
    }

    void displayMenu() {
        cout << "\nDownloads folder: " << fileSystem.getDownloadFolder() << endl;

        int choice = 0;
        do {
            cout << "\n=== File Transfer Client ===\n";
            cout << "1. Upload file\n2. Download file\n3. List server files\n";
            cout << "4. Delete server file\n5. List trash files\n6. Restore file\n7. Exit\n";
            cout << "Choice: ";
            cin >> choice;
            
            switch(choice) {
                case 1: {
                    string fn;
                    cout << "Enter path to file to upload: ";
                    cin >> fn;
                    uploadFile(fn);
                    break;
                }
                case 2: 
                    downloadFile(); 
                    break;
                case 3: 
                    listServerFiles(); 
                    break;
                case 4: 
                    deleteServerFile(); 
                    break;
                case 5: 
                    listTrashFiles(); 
                    break;
                case 6: 
                    restoreFromTrash(); 
                    break;
                case 7: 
                    cout << "Exiting\n"; 
                    break;
                default: 
                    cout << "Invalid choice\n";
            }
        } while (choice != 7);
    }

private:
    void waitForServerResponse(SOCKET sock) {
        char buf[4096];
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        
        timeval tv; 
        tv.tv_sec = 5; 
        tv.tv_usec = 0;
        
        int sel = select(0, &readSet, NULL, NULL, &tv);
        if (sel > 0) {
            int got = recv(sock, buf, sizeof(buf)-1, 0);
            if (got > 0) {
                buf[got] = '\0';
                cout << "Server: " << buf << endl;
            }
        } else if (sel == 0) {
            cout << "No reply from server after upload (timed out) — upload may have succeeded.\n";
        } else {
            cout << "Select error\n";
        }
    }

    void receiveFileData(SOCKET sock, ofstream& out) {
        char buf[4096];
        while (true) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(sock, &readSet);
            
            timeval tv; 
            tv.tv_sec = 2; 
            tv.tv_usec = 0;
            
            int sel = select(0, &readSet, NULL, NULL, &tv);
            if (sel > 0) {
                int got = recv(sock, buf, sizeof(buf), 0);
                if (got > 0) {
                    out.write(buf, got);
                } else {
                    break;
                }
            } else if (sel == 0) {
                break;
            } else {
                cout << "Select error\n";
                break;
            }
        }
    }
};

int main() {
    FTPClient client;
    
    if (!client.initialize()) {
        cout << "Failed to initialize FTP client\n";
        return 1;
    }
    
    client.displayMenu();
    return 0;
}