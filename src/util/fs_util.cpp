#include "util/fs_util.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ed::fs {

bool exists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

bool is_directory(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool is_regular_file(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool is_writable(const std::string& path) {
    return ::access(path.c_str(), W_OK) == 0;
}

bool mkdir_p(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (is_directory(path)) {
        return true;
    }
    std::string cur;
    std::size_t i = 0;
    if (path.front() == '/') {
        cur = "/";
        i = 1;
    }
    while (i <= path.size()) {
        const std::size_t next = path.find('/', i);
        const std::string part =
            path.substr(i, next == std::string::npos ? std::string::npos
                                                     : next - i);
        if (!part.empty()) {
            if (!cur.empty() && cur.back() != '/') {
                cur.push_back('/');
            }
            cur += part;
            if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        }
        if (next == std::string::npos) {
            break;
        }
        i = next + 1;
    }
    return is_directory(path);
}

std::string read_file(const std::string& path, bool* ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (ok != nullptr) {
            *ok = false;
        }
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (ok != nullptr) {
        *ok = true;
    }
    return ss.str();
}

bool write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

bool atomic_write(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t n =
            ::write(fd, data.data() + written, data.size() - written);
        if (n <= 0) {
            ::close(fd);
            ::unlink(tmp.c_str());
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp.c_str());
        return false;
    }
    ::close(fd);
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

uint64_t file_size(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

bool remove_all(const std::string& path) {
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(st.st_mode)) {
        return ::unlink(path.c_str()) == 0;
    }
    DIR* dir = ::opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }
    bool ok = true;
    while (struct dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (!remove_all(join(path, name))) {
            ok = false;
        }
    }
    ::closedir(dir);
    if (::rmdir(path.c_str()) != 0) {
        ok = false;
    }
    return ok;
}

uint64_t free_space_mb(const std::string& path) {
    struct statvfs vfs {};
    if (::statvfs(path.c_str(), &vfs) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize / (1024 * 1024);
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    if (b.front() == '/') {
        return b;
    }
    if (a.back() == '/') {
        return a + b;
    }
    return a + "/" + b;
}

std::string dirname(const std::string& path) {
    const std::size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

std::string basename(const std::string& path) {
    const std::size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

}  // namespace ed::fs
