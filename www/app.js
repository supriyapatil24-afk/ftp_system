class FTPClient {
  constructor() {
    this.baseUrl = window.location.origin;
    this.checkAuthentication();
  }

  async checkAuthentication() {
    try {
      const response = await fetch("/list");
      if (response.status === 401) {
        window.location.href = "/login";
        return;
      }
      this.initializeApp();
    } catch (error) {
      window.location.href = "/login";
    }
  }

  initializeApp() {
    this.initializeEventListeners();
    this.loadInitialData();
  }

  async apiCall(path, options = {}) {
    try {
      const response = await fetch(path, options);
      if (response.status === 401) {
        window.location.href = "/login";
        throw new Error("Unauthorized");
      }
      if (!response.ok) {
        const errorText = await response.text();
        throw new Error(`HTTP ${response.status}: ${errorText}`);
      }
      return response;
    } catch (error) {
      console.error("API call failed:", error);
      throw error;
    }
  }

  async get(path) {
    return await this.apiCall(path);
  }

  async post(path, body = null, contentType = "application/octet-stream") {
    const headers = { "Content-Type": contentType };
    return await this.apiCall(path, {
      method: "POST",
      headers,
      body: body || new Blob(),
    });
  }

  initializeEventListeners() {
    // Upload functionality
    document
      .getElementById("uploadBtn")
      .addEventListener("click", () => this.uploadFile());

    // Refresh buttons
    document
      .getElementById("refreshList")
      .addEventListener("click", () => this.refreshFiles());
    document
      .getElementById("refreshTrash")
      .addEventListener("click", () => this.refreshTrash());

    // Action buttons
    document
      .getElementById("downloadBtn")
      .addEventListener("click", () => this.downloadFile());
    document
      .getElementById("deleteBtn")
      .addEventListener("click", () => this.deleteFile());
    document
      .getElementById("restoreBtn")
      .addEventListener("click", () => this.restoreFile());

    // Trash management
    document
      .getElementById("deletePermanentBtn")
      .addEventListener("click", () => this.deletePermanent());
    document
      .getElementById("emptyTrashBtn")
      .addEventListener("click", () => this.emptyTrash());

    // Logout button
    document
      .getElementById("logoutBtn")
      .addEventListener("click", () => this.logout());

    // Enter key support for inputs
    document
      .getElementById("actionFilename")
      .addEventListener("keypress", (e) => {
        if (e.key === "Enter") this.handleActionInput();
      });

    document
      .getElementById("trashFilename")
      .addEventListener("keypress", (e) => {
        if (e.key === "Enter") this.handleTrashInput();
      });
  }

  loadInitialData() {
    this.refreshFiles();
    this.refreshTrash();
  }

  async refreshFiles() {
    try {
      const response = await this.get("/list");
      const text = await response.text();
      this.updateFileList("fileList", text);
    } catch (error) {
      this.showError("fileList", "Error loading files: " + error.message);
    }
  }

  async refreshTrash() {
    try {
      const response = await this.get("/list_trash");
      const text = await response.text();
      this.updateFileList("trashList", text);
    } catch (error) {
      this.showError("trashList", "Error loading trash: " + error.message);
    }
  }

  updateFileList(elementId, fileText) {
    const ul = document.getElementById(elementId);
    ul.innerHTML = "";

    const files = fileText
      .split("\n")
      .filter((line) => line.trim() && !line.startsWith("==="));

    if (files.length === 0) {
      const li = document.createElement("li");
      li.textContent = "No files found";
      li.style.color = "#666";
      li.style.fontStyle = "italic";
      ul.appendChild(li);
      return;
    }

    files.forEach((file) => {
      if (file.trim()) {
        const li = document.createElement("li");
        li.textContent = file.trim();

        // Add click to fill filename
        li.style.cursor = "pointer";
        li.addEventListener("click", () => {
          const filename = file.trim();
          document.getElementById("actionFilename").value = filename;
          document.getElementById("trashFilename").value = filename;
        });

        ul.appendChild(li);
      }
    });
  }

  showError(elementId, message) {
    const element = document.getElementById(elementId);
    if (element) {
      element.textContent = message;
      element.style.color = "#e74c3c";
    }
  }

  showStatus(elementId, message, isError = false) {
    const element = document.getElementById(elementId);
    if (element) {
      element.textContent = message;
      element.style.color = isError ? "#e74c3c" : "#27ae60";
    }
  }

  async uploadFile() {
    const input = document.getElementById("uploadFile");
    const status = document.getElementById("uploadStatus");

    if (!input.files.length) {
      this.showStatus("uploadStatus", "Please select a file first.", true);
      return;
    }

    const file = input.files[0];
    this.showStatus("uploadStatus", `Uploading ${file.name}...`);

    try {
      const url = `/upload?filename=${encodeURIComponent(file.name)}`;
      await this.post(url, file);
      this.showStatus("uploadStatus", `✅ Successfully uploaded: ${file.name}`);
      input.value = ""; // Clear file input
      await this.refreshFiles();
    } catch (error) {
      this.showStatus(
        "uploadStatus",
        `❌ Upload failed: ${error.message}`,
        true
      );
    }
  }

  async downloadFile() {
    const filename = document.getElementById("actionFilename").value.trim();
    const status = document.getElementById("actionStatus");

    if (!filename) {
      this.showStatus("actionStatus", "Please enter a filename.", true);
      return;
    }

    this.showStatus("actionStatus", `Downloading ${filename}...`);

    try {
      const response = await this.get(
        `/download?file=${encodeURIComponent(filename)}`
      );
      const blob = await response.blob();

      // Create download link
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);

      this.showStatus("actionStatus", `✅ Download started: ${filename}`);
    } catch (error) {
      this.showStatus(
        "actionStatus",
        `❌ Download failed: ${error.message}`,
        true
      );
    }
  }

  async deleteFile() {
    const filename = document.getElementById("actionFilename").value.trim();
    const status = document.getElementById("actionStatus");

    if (!filename) {
      this.showStatus("actionStatus", "Please enter a filename.", true);
      return;
    }

    if (!confirm(`Are you sure you want to move "${filename}" to trash?`)) {
      return;
    }

    this.showStatus("actionStatus", `Moving ${filename} to trash...`);

    try {
      await this.post(`/delete?filename=${encodeURIComponent(filename)}`);
      this.showStatus("actionStatus", `✅ Moved to trash: ${filename}`);
      await this.refreshFiles();
      await this.refreshTrash();
    } catch (error) {
      this.showStatus(
        "actionStatus",
        `❌ Delete failed: ${error.message}`,
        true
      );
    }
  }

  async restoreFile() {
    const filename = document.getElementById("actionFilename").value.trim();
    const status = document.getElementById("actionStatus");

    if (!filename) {
      this.showStatus("actionStatus", "Please enter a filename.", true);
      return;
    }

    this.showStatus("actionStatus", `Restoring ${filename}...`);

    try {
      await this.post(`/restore?filename=${encodeURIComponent(filename)}`);
      this.showStatus("actionStatus", `✅ Restored: ${filename}`);
      await this.refreshFiles();
      await this.refreshTrash();
    } catch (error) {
      this.showStatus(
        "actionStatus",
        `❌ Restore failed: ${error.message}`,
        true
      );
    }
  }

  async deletePermanent() {
    const filename = document.getElementById("trashFilename").value.trim();
    const status = document.getElementById("trashStatus");

    if (!filename) {
      this.showStatus("trashStatus", "Please enter a filename.", true);
      return;
    }

    if (
      !confirm(
        `Permanently delete "${filename}"? This action cannot be undone!`
      )
    ) {
      return;
    }

    this.showStatus("trashStatus", `Permanently deleting ${filename}...`);

    try {
      await this.post(
        `/delete_permanent?filename=${encodeURIComponent(filename)}`
      );
      this.showStatus("trashStatus", `✅ Permanently deleted: ${filename}`);
      await this.refreshTrash();
    } catch (error) {
      this.showStatus(
        "trashStatus",
        `❌ Permanent delete failed: ${error.message}`,
        true
      );
    }
  }

  async emptyTrash() {
    const status = document.getElementById("trashStatus");

    if (
      !confirm(
        "Empty entire trash? This will permanently delete ALL files in trash!"
      )
    ) {
      return;
    }

    this.showStatus("trashStatus", "Emptying trash...");

    try {
      await this.post("/empty_trash");
      this.showStatus("trashStatus", "✅ Trash emptied successfully");
      await this.refreshTrash();
    } catch (error) {
      this.showStatus(
        "trashStatus",
        `❌ Empty trash failed: ${error.message}`,
        true
      );
    }
  }

  async logout() {
    try {
      await fetch("/logout");
      window.location.href = "/login";
    } catch (error) {
      console.error("Logout failed:", error);
      window.location.href = "/login";
    }
  }

  handleActionInput() {
    this.downloadFile();
  }

  handleTrashInput() {
    this.deletePermanent();
  }
}

// Initialize the FTP client when page loads
document.addEventListener("DOMContentLoaded", () => {
  new FTPClient();

  // Add some helpful tips
  console.log("🚀 FTP Client initialized");
  console.log(
    "💡 Tip: Click on file names in the lists to auto-fill the filename inputs"
  );
  console.log(
    "💡 Tip: Press Enter in filename inputs to trigger the most common action"
  );
});
