/**
 * @file MappedFile.hpp
 * @brief Opening an image without reading it.
 *
 * The symmetric half of @ref CatalogueStream, and it exists for the same measured reason.
 * Streaming the WRITE brought a nineteen-million-row bake down from a projected 11.9 GB of
 * memory to 7 MB — and then `lpl-ask` opened the 3.71 GB image it produced by slurping the
 * whole thing, at 4.2 GB of resident memory. Solving one end and not the other leaves the
 * ceiling exactly where it was, one process further along.
 *
 * `KnowledgePack::open` takes a pointer and a length and never copies, which is what makes
 * this a three-line change rather than a format change: a mapping IS a pointer and a length.
 * That property was already true and already deliberate — it is why the ring-0 reader can open
 * an image sitting in a boot module without moving it — and this is a second thing it buys.
 *
 * @warning Host only. `mmap` is POSIX and this module is the hosted half of the repository; the
 * freestanding reader is handed bytes by whatever already has them.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#pragma once

#ifndef LPL_LPL_HARVEST_MAPPEDFILE_HPP
#    define LPL_LPL_HARVEST_MAPPEDFILE_HPP

#    include <lpl/Foundation.hpp>

#    include <string>

namespace lpl::harvest {

/**
 * @class MappedFile
 * @brief A read-only mapping of a file, released when it goes out of scope.
 */
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;

    /**
     * @brief Maps a file read-only.
     *
     * @param path Where.
     * @return false when the file could not be opened, is empty, or could not be mapped.
     */
    [[nodiscard]] bool open(const std::string &path);

    /**
     * @brief Releases the mapping.
     */
    void close() noexcept;

    /**
     * @brief The mapped bytes.
     *
     * @return The pointer, or nullptr when nothing is mapped.
     */
    [[nodiscard]] const core::u8 *bytes() const noexcept { return _bytes; }

    /**
     * @brief How many.
     *
     * @warning A `core::u32`, deliberately narrowed: the format's offsets are 32-bit words, so an
     * image larger than four gigabytes is one this reader cannot address anyway. A file past
     * that is refused by @ref open rather than mapped and half-read.
     *
     * @return The length.
     */
    [[nodiscard]] core::u32 size() const noexcept { return _size; }

private:
    const core::u8 *_bytes{nullptr};
    core::u32 _size{0u};
    core::u64 _mapped{0u}; ///< What was passed to mmap, which is what must be unmapped.
};

} // namespace lpl::harvest

#endif // LPL_LPL_HARVEST_MAPPEDFILE_HPP
