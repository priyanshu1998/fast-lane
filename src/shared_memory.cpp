#include "fast_lane/shared_memory.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fast_lane
{
namespace
{

constexpr std::uint32_t kMagic = 0x484d5346; // "FSMH" in little-endian memory.
constexpr std::uint32_t kAbiVersion = 1;
constexpr std::size_t kMaxReadRetries = 8;

struct SharedHeader
{
    std::uint32_t magic = kMagic;
    std::uint32_t abi_version = kAbiVersion;
    std::uint64_t capacity = 0;
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint64_t> bytes_used{0};
};

std::size_t checked_region_size(std::size_t capacity)
{
    if (capacity > std::numeric_limits<std::size_t>::max() - sizeof(SharedHeader))
    {
        throw SharedMemoryError("shared-memory capacity is too large");
    }
    return sizeof(SharedHeader) + capacity;
}

std::string platform_name(const std::string& name)
{
    if (name.empty())
    {
        throw SharedMemoryError("shared-memory name must not be empty");
    }

#ifdef _WIN32
    if (name.rfind("Global\\", 0) == 0 || name.rfind("Local\\", 0) == 0)
    {
        return name;
    }
    return "Local\\" + name;
#else
    if (name.front() == '/')
    {
        return name;
    }
    return "/" + name;
#endif
}

#ifdef _WIN32
std::string last_error_message(const std::string& prefix)
{
    const DWORD code = GetLastError();
    LPSTR buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    std::ostringstream out;
    out << prefix << " (Win32 error " << code << ")";
    if (length > 0 && buffer != nullptr)
    {
        out << ": " << buffer;
        LocalFree(buffer);
    }
    return out.str();
}
#else
std::string errno_message(const std::string& prefix)
{
    std::ostringstream out;
    out << prefix << ": " << std::strerror(errno);
    return out.str();
}
#endif

class MemoryMapping
{
  public:
    static MemoryMapping create(const std::string& requested_name, std::size_t size)
    {
        const std::string mapped_name = platform_name(requested_name);

#ifdef _WIN32
        if (size > static_cast<std::uint64_t>(std::numeric_limits<DWORD>::max()) * 2ULL + 1ULL)
        {
            throw SharedMemoryError("mapping size is too large for this demo");
        }

        const DWORD size_high =
            static_cast<DWORD>((static_cast<std::uint64_t>(size) >> 32) & 0xffffffffULL);
        const DWORD size_low = static_cast<DWORD>(static_cast<std::uint64_t>(size) & 0xffffffffULL);

        HANDLE handle = CreateFileMappingA(INVALID_HANDLE_VALUE,
                                           nullptr,
                                           PAGE_READWRITE,
                                           size_high,
                                           size_low,
                                           mapped_name.c_str());
        if (handle == nullptr)
        {
            throw SharedMemoryError(last_error_message("CreateFileMapping failed"));
        }

        void* view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (view == nullptr)
        {
            CloseHandle(handle);
            throw SharedMemoryError(last_error_message("MapViewOfFile failed"));
        }

        return MemoryMapping(requested_name, mapped_name, view, size, handle, true);
#else
        shm_unlink(mapped_name.c_str());

        const int fd = shm_open(mapped_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd == -1)
        {
            throw SharedMemoryError(errno_message("shm_open create failed"));
        }

        if (ftruncate(fd, static_cast<off_t>(size)) == -1)
        {
            close(fd);
            shm_unlink(mapped_name.c_str());
            throw SharedMemoryError(errno_message("ftruncate failed"));
        }

        void* view = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (view == MAP_FAILED)
        {
            close(fd);
            shm_unlink(mapped_name.c_str());
            throw SharedMemoryError(errno_message("mmap create failed"));
        }

        return MemoryMapping(requested_name, mapped_name, view, size, fd, true);
#endif
    }

    static MemoryMapping open(const std::string& requested_name)
    {
        const std::string mapped_name = platform_name(requested_name);

#ifdef _WIN32
        HANDLE handle = OpenFileMappingA(FILE_MAP_READ, FALSE, mapped_name.c_str());
        if (handle == nullptr)
        {
            throw SharedMemoryError(last_error_message("OpenFileMapping failed"));
        }

        void* view = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0);
        if (view == nullptr)
        {
            CloseHandle(handle);
            throw SharedMemoryError(last_error_message("MapViewOfFile read-only failed"));
        }

        const auto* header = static_cast<const SharedHeader*>(view);
        const std::size_t size = checked_region_size(static_cast<std::size_t>(header->capacity));

        return MemoryMapping(requested_name, mapped_name, view, size, handle, false);
#else
        const int fd = shm_open(mapped_name.c_str(), O_RDONLY, 0);
        if (fd == -1)
        {
            throw SharedMemoryError(errno_message("shm_open follower failed"));
        }

        struct stat info{};
        if (fstat(fd, &info) == -1)
        {
            close(fd);
            throw SharedMemoryError(errno_message("fstat failed"));
        }
        const std::size_t size = static_cast<std::size_t>(info.st_size);

        void* view = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (view == MAP_FAILED)
        {
            close(fd);
            throw SharedMemoryError(errno_message("mmap follower failed"));
        }

        return MemoryMapping(requested_name, mapped_name, view, size, fd, false);
#endif
    }

    MemoryMapping(MemoryMapping&& other) noexcept
        : requested_name_(std::move(other.requested_name_))
        , mapped_name_(std::move(other.mapped_name_))
        , view_(other.view_)
        , size_(other.size_)
        ,
#ifdef _WIN32
        handle_(other.handle_)
        ,
#else
        fd_(other.fd_)
        ,
#endif
        owner_(other.owner_)
    {
        other.view_ = nullptr;
        other.size_ = 0;
#ifdef _WIN32
        other.handle_ = nullptr;
#else
        other.fd_ = -1;
#endif
        other.owner_ = false;
    }

    MemoryMapping& operator=(MemoryMapping&& other) noexcept
    {
        if (this != &other)
        {
            cleanup();

            requested_name_ = std::move(other.requested_name_);
            mapped_name_ = std::move(other.mapped_name_);
            view_ = other.view_;
            size_ = other.size_;
#ifdef _WIN32
            handle_ = other.handle_;
#else
            fd_ = other.fd_;
#endif
            owner_ = other.owner_;

            other.view_ = nullptr;
            other.size_ = 0;
#ifdef _WIN32
            other.handle_ = nullptr;
#else
            other.fd_ = -1;
#endif
            other.owner_ = false;
        }
        return *this;
    }

    MemoryMapping(const MemoryMapping&) = delete;
    MemoryMapping& operator=(const MemoryMapping&) = delete;

    ~MemoryMapping()
    {
        cleanup();
    }

    void* view() const
    {
        return view_;
    }

    std::size_t size() const
    {
        return size_;
    }

    const std::string& requested_name() const
    {
        return requested_name_;
    }

  private:
#ifdef _WIN32
    MemoryMapping(std::string requested_name,
                  std::string mapped_name,
                  void* view,
                  std::size_t size,
                  HANDLE handle,
                  bool owner)
#else
    MemoryMapping(std::string requested_name,
                  std::string mapped_name,
                  void* view,
                  std::size_t size,
                  int fd,
                  bool owner)
#endif
        : requested_name_(std::move(requested_name))
        , mapped_name_(std::move(mapped_name))
        , view_(view)
        , size_(size)
        ,
#ifdef _WIN32
        handle_(handle)
        ,
#else
        fd_(fd)
        ,
#endif
        owner_(owner)
    {
    }

    void cleanup()
    {
#ifdef _WIN32
        if (view_ != nullptr)
        {
            UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (handle_ != nullptr)
        {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#else
        if (view_ != nullptr)
        {
            munmap(view_, size_);
            view_ = nullptr;
        }
        if (fd_ != -1)
        {
            close(fd_);
            fd_ = -1;
        }
        if (owner_)
        {
            shm_unlink(mapped_name_.c_str());
            owner_ = false;
        }
#endif
    }

    std::string requested_name_;
    std::string mapped_name_;
    void* view_ = nullptr;
    std::size_t size_ = 0;
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    bool owner_ = false;
};

void validate_header(const SharedHeader& header, std::size_t mapped_size)
{
    if (header.magic != kMagic)
    {
        throw SharedMemoryError("shared-memory header magic does not match");
    }
    if (header.abi_version != kAbiVersion)
    {
        throw SharedMemoryError("shared-memory ABI version does not match");
    }
    if (checked_region_size(static_cast<std::size_t>(header.capacity)) > mapped_size)
    {
        throw SharedMemoryError("shared-memory header capacity exceeds mapped size");
    }
}

std::uint8_t* payload_begin(SharedHeader* header)
{
    return reinterpret_cast<std::uint8_t*>(header) + sizeof(SharedHeader);
}

const std::uint8_t* payload_begin(const SharedHeader* header)
{
    return reinterpret_cast<const std::uint8_t*>(header) + sizeof(SharedHeader);
}

} // namespace

struct SharedMemoryChannel::Impl
{
    MemoryMapping mapping;
    SharedHeader* header = nullptr;
    bool writable = false;

    Impl(MemoryMapping memory_mapping, bool can_write)
        : mapping(std::move(memory_mapping))
        , header(static_cast<SharedHeader*>(mapping.view()))
        , writable(can_write)
    {
    }
};

SharedMemoryError::SharedMemoryError(const std::string& message)
    : std::runtime_error(message)
{
}

SharedMemoryChannel SharedMemoryChannel::create_leader(const std::string& name,
                                                       std::size_t capacity)
{
    const std::size_t size = checked_region_size(capacity);
    auto mapping = MemoryMapping::create(name, size);

    auto* header = new (mapping.view()) SharedHeader();
    header->capacity = static_cast<std::uint64_t>(capacity);
    header->sequence.store(0, std::memory_order_release);
    header->bytes_used.store(0, std::memory_order_release);

    return SharedMemoryChannel(std::make_unique<Impl>(std::move(mapping), true));
}

SharedMemoryChannel SharedMemoryChannel::open_follower(const std::string& name)
{
    auto mapping = MemoryMapping::open(name);
    auto* header = static_cast<const SharedHeader*>(mapping.view());
    validate_header(*header, mapping.size());
    return SharedMemoryChannel(std::make_unique<Impl>(std::move(mapping), false));
}

SharedMemoryChannel::SharedMemoryChannel(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

SharedMemoryChannel::SharedMemoryChannel(SharedMemoryChannel&& other) noexcept = default;

SharedMemoryChannel& SharedMemoryChannel::operator=(SharedMemoryChannel&& other) noexcept = default;

SharedMemoryChannel::~SharedMemoryChannel() = default;

std::size_t SharedMemoryChannel::capacity() const
{
    return static_cast<std::size_t>(impl_->header->capacity);
}

std::string SharedMemoryChannel::name() const
{
    return impl_->mapping.requested_name();
}

void SharedMemoryChannel::publish(const std::vector<std::uint8_t>& bytes)
{
    if (!impl_->writable)
    {
        throw SharedMemoryError("followers cannot publish to shared memory");
    }
    if (bytes.size() > capacity())
    {
        throw SharedMemoryError("payload exceeds shared-memory capacity");
    }

    SharedHeader* header = impl_->header;
    const std::uint64_t current = header->sequence.load(std::memory_order_relaxed);
    const std::uint64_t writing_sequence = (current % 2 == 0) ? current + 1 : current + 2;

    header->sequence.store(writing_sequence, std::memory_order_release);
    std::memcpy(payload_begin(header), bytes.data(), bytes.size());
    header->bytes_used.store(static_cast<std::uint64_t>(bytes.size()), std::memory_order_release);
    header->sequence.store(writing_sequence + 1, std::memory_order_release);
}

bool SharedMemoryChannel::try_read(std::uint64_t last_seen_sequence, SharedSnapshot& out) const
{
    const SharedHeader* header = impl_->header;

    for (std::size_t attempt = 0; attempt < kMaxReadRetries; ++attempt)
    {
        const std::uint64_t before = header->sequence.load(std::memory_order_acquire);
        if (before % 2 != 0)
        {
            std::this_thread::yield();
            continue;
        }
        if (before == last_seen_sequence)
        {
            return false;
        }

        const std::uint64_t bytes_used = header->bytes_used.load(std::memory_order_acquire);
        if (bytes_used > header->capacity)
        {
            throw SharedMemoryError("shared-memory payload length is invalid");
        }

        std::vector<std::uint8_t> copy(static_cast<std::size_t>(bytes_used));
        std::memcpy(copy.data(), payload_begin(header), copy.size());

        const std::uint64_t after = header->sequence.load(std::memory_order_acquire);
        if (before == after && after % 2 == 0)
        {
            out.sequence = after;
            out.bytes = std::move(copy);
            return true;
        }

        std::this_thread::yield();
    }

    return false;
}

CopyOnWriteBuffer::CopyOnWriteBuffer()
    : bytes_(std::make_shared<std::vector<std::uint8_t>>())
{
}

CopyOnWriteBuffer::CopyOnWriteBuffer(std::vector<std::uint8_t> bytes)
    : bytes_(std::make_shared<std::vector<std::uint8_t>>(std::move(bytes)))
{
}

CopyOnWriteBuffer CopyOnWriteBuffer::fork() const
{
    CopyOnWriteBuffer copy;
    copy.bytes_ = bytes_;
    return copy;
}

bool CopyOnWriteBuffer::empty() const
{
    return bytes_->empty();
}

std::size_t CopyOnWriteBuffer::size() const
{
    return bytes_->size();
}

const std::uint8_t* CopyOnWriteBuffer::data() const
{
    return bytes_->data();
}

std::uint8_t CopyOnWriteBuffer::at(std::size_t offset) const
{
    return bytes_->at(offset);
}

void CopyOnWriteBuffer::set(std::size_t offset, std::uint8_t value)
{
    detach();
    bytes_->at(offset) = value;
}

void CopyOnWriteBuffer::append(std::uint8_t value)
{
    detach();
    bytes_->push_back(value);
}

void CopyOnWriteBuffer::detach()
{
    if (!bytes_.unique())
    {
        bytes_ = std::make_shared<std::vector<std::uint8_t>>(*bytes_);
    }
}

std::vector<std::uint8_t> encode_u64_sequence(const std::vector<std::uint64_t>& values)
{
    std::vector<std::uint8_t> bytes(values.size() * sizeof(std::uint64_t));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    return bytes;
}

std::vector<std::uint64_t> decode_u64_sequence(const std::vector<std::uint8_t>& bytes)
{
    const std::size_t count = bytes.size() / sizeof(std::uint64_t);
    std::vector<std::uint64_t> values(count);
    std::memcpy(values.data(), bytes.data(), values.size() * sizeof(std::uint64_t));
    return values;
}

} // namespace fast_lane
