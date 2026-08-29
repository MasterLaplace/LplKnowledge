/**
 * @file MappedFile.cpp
 * @brief Implementation of opening an image without reading it.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/MappedFile.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lpl::harvest {

MappedFile::~MappedFile() { close(); }

void MappedFile::close() noexcept
{
    if (_bytes != nullptr && _mapped != 0u)
        (void) ::munmap(const_cast<void *>(static_cast<const void *>(_bytes)),
                        static_cast<std::size_t>(_mapped));
    _bytes = nullptr;
    _size = 0u;
    _mapped = 0u;
}

bool MappedFile::open(const std::string &path)
{
    close();

    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0)
        return false;

    struct stat status {};
    if (::fstat(descriptor, &status) != 0 || status.st_size <= 0)
    {
        (void) ::close(descriptor);
        return false;
    }

    // @warning Refused rather than truncated. The format addresses itself with 32-bit offsets, so a
    // larger file is one no reader can navigate; mapping its first four gigabytes would produce
    // a pack that opens, answers, and is wrong about where every section ends.
    const auto length = static_cast<core::u64>(status.st_size);
    if (length > 0xFFFFFFFFu)
    {
        (void) ::close(descriptor);
        return false;
    }

    void *mapping = ::mmap(nullptr, static_cast<std::size_t>(length), PROT_READ, MAP_PRIVATE, descriptor, 0);
    // The descriptor is not needed once the mapping exists: a mapping holds its own reference
    // to the file, so closing here means a long-lived reader does not also hold a descriptor.
    (void) ::close(descriptor);
    if (mapping == MAP_FAILED)
        return false;

    // The access pattern is a scan: a catalogue query walks every row once. Saying so lets the
    // kernel read ahead and, more importantly, drop pages behind — which is what keeps resident
    // memory flat instead of growing to the size of the file as it is touched.
    (void) ::madvise(mapping, static_cast<std::size_t>(length), MADV_SEQUENTIAL);

    _bytes = static_cast<const core::u8 *>(mapping);
    _size = static_cast<core::u32>(length);
    _mapped = length;
    return true;
}

} // namespace lpl::harvest
