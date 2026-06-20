// ============================================================================
// gitpp  --  Modern C++20 wrapper around libgit2
//
// Features:
//   o  RAII via std::unique_ptr with custom deleters -- no manual free()
//   o  std::source_location integrated error reporting
//   o  constexpr oid with C++20 spaceship operator  <=>
//   o  [[nodiscard]] throughout
//   o  std::span for buffer views
//   o  No boost dependency
// ============================================================================

#pragma once

#include <git2.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <compare>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

// C++20 std::source_location (clang 15+ / libc++ 16+)
#if __has_include(<source_location>)
#  include <source_location>
#  define GITPP_HAS_SOURCE_LOCATION 1
#else
#  define GITPP_HAS_SOURCE_LOCATION 0
#  include <cstdint>
#endif

#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef GITPP_NODISCARD
#define GITPP_NODISCARD [[nodiscard]]
#endif

namespace gitpp {

// Forward declarations
class repo;
class oid;
class reference;
class object;
class blob;
class commit;
class tree;
class tree_entry;
class status_list;
class index;
class signature;
class remote;
class revwalk;
class branch;
class branch_iterator;
class tag;
class diff;

// ---------------------------------------------------------------------------
// RAII helper: generic deleter for libgit2 resources
// ---------------------------------------------------------------------------

template <auto FreeFn>
struct git_deleter {
    template <typename T>
    void operator()(T* p) const noexcept {
        if (p) FreeFn(p);
    }
};

using unique_repository      = std::unique_ptr<git_repository,     git_deleter<git_repository_free>>;
using unique_reference       = std::unique_ptr<git_reference,      git_deleter<git_reference_free>>;
using unique_status_list     = std::unique_ptr<git_status_list,    git_deleter<git_status_list_free>>;
using unique_index           = std::unique_ptr<git_index,          git_deleter<git_index_free>>;
using unique_signature       = std::unique_ptr<git_signature,      git_deleter<git_signature_free>>;
using unique_remote          = std::unique_ptr<git_remote,         git_deleter<git_remote_free>>;
using unique_revwalk         = std::unique_ptr<git_revwalk,        git_deleter<git_revwalk_free>>;
using unique_branch_iterator = std::unique_ptr<git_branch_iterator, git_deleter<git_branch_iterator_free>>;
using unique_diff            = std::unique_ptr<git_diff,           git_deleter<git_diff_free>>;
using unique_tag             = std::unique_ptr<git_tag,            git_deleter<git_tag_free>>;

// Deleter for git_object hierarchy (commit, tree, blob, tag)
template <typename T>
struct object_deleter {
    void operator()(T* p) const noexcept {
        if (p) git_object_free(reinterpret_cast<git_object*>(p));
    }
};

template <typename T>
using unique_git_object = std::unique_ptr<T, object_deleter<T>>;

// ---------------------------------------------------------------------------
// Exception types with C++20 source_location
// ---------------------------------------------------------------------------

namespace exception {

struct git2_exception : public std::exception {
    const char* what() const noexcept override { return "libgit2 exception"; }
};

struct not_repo : public git2_exception {
    const char* what() const noexcept override { return "not a git repository"; }
};

struct resolve_failed : public git2_exception {
    const char* what() const noexcept override { return "reference resolution failed"; }
};

#if GITPP_HAS_SOURCE_LOCATION

class error : public std::exception {
public:
    explicit error(std::source_location loc = std::source_location::current()) noexcept;
    explicit error(int code, std::source_location loc = std::source_location::current()) noexcept;
    const char* what() const noexcept override { return msg_.c_str(); }
    int code() const noexcept { return code_; }
    const std::source_location& where() const noexcept { return loc_; }
private:
    int code_ = -1;
    std::string msg_;
    std::source_location loc_;
};

[[noreturn]] void throw_error(int code, std::source_location loc = std::source_location::current());

inline void throw_on_error(int code, std::source_location loc = std::source_location::current()) {
    if (code < 0)
        throw_error(code, std::move(loc));
}

#else // fallback without source_location

class error : public std::exception {
public:
    error() noexcept : error(-1) {}
    explicit error(int code) noexcept;
    const char* what() const noexcept override { return msg_.c_str(); }
    int code() const noexcept { return code_; }
private:
    int code_ = -1;
    std::string msg_;
};

[[noreturn]] void throw_error(int code);

inline void throw_on_error(int code) {
    if (code < 0)
        throw_error(code);
}

#endif

} // namespace exception

// ---------------------------------------------------------------------------
// oid -- Git object ID (SHA-1), fully constexpr
// ---------------------------------------------------------------------------

class oid {
public:
    static constexpr std::size_t hash_size = 20;
    static constexpr std::size_t hex_size  = 40;

    constexpr oid() noexcept = default;

    constexpr explicit oid(const git_oid* src) noexcept {
        if (src)
            std::copy(std::begin(src->id), std::end(src->id), std::begin(oid_.id));
    }

    GITPP_NODISCARD constexpr const git_oid* native() const noexcept { return &oid_; }
    GITPP_NODISCARD constexpr       git_oid* native()       noexcept { return &oid_; }
    GITPP_NODISCARD constexpr const unsigned char* data() const noexcept { return oid_.id; }
    GITPP_NODISCARD constexpr       unsigned char* data()       noexcept { return oid_.id; }
    GITPP_NODISCARD constexpr std::span<const unsigned char, hash_size> bytes() const noexcept {
        return {oid_.id};
    }

    // C++20 spaceship operator
    // Manual constexpr loop (std::memcmp is not constexpr on all platforms)
    GITPP_NODISCARD constexpr std::strong_ordering operator<=>(const oid& other) const noexcept {
        for (std::size_t i = 0; i < hash_size; ++i) {
            if (oid_.id[i] != other.oid_.id[i])
                return oid_.id[i] <=> other.oid_.id[i];
        }
        return std::strong_ordering::equal;
    }
    GITPP_NODISCARD constexpr bool operator==(const oid& other) const noexcept {
        for (std::size_t i = 0; i < hash_size; ++i) {
            if (oid_.id[i] != other.oid_.id[i])
                return false;
        }
        return true;
    }
    GITPP_NODISCARD constexpr bool operator!=(const oid& other) const noexcept {
        return !(*this == other);
    }
    GITPP_NODISCARD constexpr bool is_zero() const noexcept {
        return std::all_of(std::begin(oid_.id), std::end(oid_.id),
                           [](auto b) noexcept { return b == 0; });
    }
    GITPP_NODISCARD constexpr explicit operator bool() const noexcept { return !is_zero(); }

    GITPP_NODISCARD std::string to_string() const;
    GITPP_NODISCARD static oid from_string(std::string_view hex);

private:
    git_oid oid_{};
};

static_assert(std::is_trivially_copyable_v<oid>);

// ---------------------------------------------------------------------------
// reference
// ---------------------------------------------------------------------------

class reference {
public:
    reference() noexcept = default;
    explicit reference(git_reference* ref) noexcept;

    GITPP_NODISCARD git_reference_t type() const noexcept;
    GITPP_NODISCARD oid            target() const;
    GITPP_NODISCARD reference      resolve() const;
    GITPP_NODISCARD std::string    name() const;
    GITPP_NODISCARD std::string    shorthand() const;
    GITPP_NODISCARD explicit operator bool() const noexcept { return ref_ != nullptr; }
    GITPP_NODISCARD git_reference*       native()       noexcept { return ref_.get(); }
    GITPP_NODISCARD const git_reference* native() const noexcept { return ref_.get(); }

private:
    unique_reference ref_;
};

// ---------------------------------------------------------------------------
// object hierarchy (object -> blob, commit, tree)
// ---------------------------------------------------------------------------

class object {
public:
    object() noexcept = default;
    virtual ~object() = default;
    object(const object&) = delete;
    object& operator=(const object&) = delete;
    object(object&&) noexcept = default;
    object& operator=(object&&) noexcept = default;

    GITPP_NODISCARD oid            id() const noexcept;
    GITPP_NODISCARD git_object_t   type() const noexcept;
    GITPP_NODISCARD git_object*       native()       noexcept { return obj_.get(); }
    GITPP_NODISCARD const git_object* native() const noexcept { return obj_.get(); }

protected:
    explicit object(git_object* obj) noexcept : obj_(obj) {}
    unique_git_object<git_object> obj_;
};

// --- blob ------------------------------------------------------------------

class blob : public object {
public:
    blob() noexcept = default;
    explicit blob(git_blob* b) noexcept;

    GITPP_NODISCARD std::span<const std::byte> content() const noexcept;
    GITPP_NODISCARD std::string_view           text() const noexcept;
    GITPP_NODISCARD std::size_t                size() const noexcept;
    GITPP_NODISCARD const git_blob* native_blob() const noexcept;
    GITPP_NODISCARD       git_blob* native_blob()       noexcept;
};

// --- commit ----------------------------------------------------------------

class commit : public object {
public:
    commit() noexcept = default;
    explicit commit(git_commit* c) noexcept;

    GITPP_NODISCARD std::string_view message()        const noexcept;
    GITPP_NODISCARD oid             tree_id()         const;
    GITPP_NODISCARD git_time_t      time()            const noexcept;
    GITPP_NODISCARD std::string     author_name()     const;
    GITPP_NODISCARD std::string     committer_name()  const;
    GITPP_NODISCARD const git_commit* native_commit() const noexcept;
    GITPP_NODISCARD       git_commit* native_commit()       noexcept;
};

// --- tree_entry ------------------------------------------------------------

class tree_entry {
public:
    tree_entry() noexcept = default;
    explicit tree_entry(const git_tree_entry* entry) noexcept;
    explicit tree_entry(git_tree_entry* entry) noexcept;
    ~tree_entry() noexcept;

    tree_entry(const tree_entry& other) noexcept;
    tree_entry(tree_entry&& other) noexcept;
    tree_entry& operator=(const tree_entry& other) noexcept;
    tree_entry& operator=(tree_entry&& other) noexcept;

    void reset() noexcept;

    GITPP_NODISCARD oid             id()        const;
    GITPP_NODISCARD git_object_t    type()      const noexcept;
    GITPP_NODISCARD std::string_view name()     const noexcept;
    GITPP_NODISCARD git_filemode_t  filemode()  const noexcept;
    GITPP_NODISCARD explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
    git_tree_entry* entry_ = nullptr;
    bool owned_ = false;
};

// --- tree ------------------------------------------------------------------

class tree : public object {
public:
    tree() noexcept = default;
    explicit tree(git_tree* t) noexcept;

    GITPP_NODISCARD tree_entry   by_path(std::string_view path) const;
    GITPP_NODISCARD tree_entry   by_index(std::size_t idx) const noexcept;
    GITPP_NODISCARD std::size_t  count() const noexcept;
    GITPP_NODISCARD const git_tree* native_tree() const noexcept;
    GITPP_NODISCARD       git_tree* native_tree()       noexcept;

    class iterator {
    public:
        using value_type        = tree_entry;
        using difference_type   = std::ptrdiff_t;
        using reference         = const tree_entry&;
        using pointer           = const tree_entry*;
        using iterator_category = std::input_iterator_tag;

        GITPP_NODISCARD tree_entry operator*()  const noexcept;
        GITPP_NODISCARD pointer    operator->() const noexcept { return &entry_; }
        iterator& operator++() noexcept;
        iterator  operator++(int) noexcept { auto t = *this; ++(*this); return t; }
        GITPP_NODISCARD friend bool operator==(const iterator& a, const iterator& b) noexcept {
            return a.parent_ == b.parent_ && a.index_ == b.index_;
        }
        GITPP_NODISCARD friend bool operator!=(const iterator& a, const iterator& b) noexcept {
            return !(a == b);
        }
    private:
        friend class tree;
        iterator(const tree* parent, std::size_t index) noexcept;
        const tree* parent_ = nullptr;
        std::size_t index_ = 0;
        mutable tree_entry entry_;
    };

    GITPP_NODISCARD iterator begin() const noexcept;
    GITPP_NODISCARD iterator end()   const noexcept;
};

// ---------------------------------------------------------------------------
// status_list
// ---------------------------------------------------------------------------

class status_list {
public:
    status_list() noexcept = default;
    explicit status_list(git_status_list* sl) noexcept;
    status_list(const status_list&) = delete;
    status_list& operator=(const status_list&) = delete;
    status_list(status_list&&) noexcept = default;
    status_list& operator=(status_list&&) noexcept = default;

    GITPP_NODISCARD std::size_t size() const noexcept;
    GITPP_NODISCARD git_status_list*       native()       noexcept { return sl_.get(); }
    GITPP_NODISCARD const git_status_list* native() const noexcept { return sl_.get(); }

    class iterator {
    public:
        using value_type        = const git_status_entry*;
        using reference         = const git_status_entry*;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;

        GITPP_NODISCARD reference operator*()  const noexcept;
        iterator& operator++() noexcept;
        iterator  operator++(int) noexcept { auto t = *this; ++(*this); return t; }
        GITPP_NODISCARD friend bool operator==(const iterator& a, const iterator& b) noexcept {
            return a.parent_ == b.parent_ && a.idx_ == b.idx_;
        }
        GITPP_NODISCARD friend bool operator!=(const iterator& a, const iterator& b) noexcept {
            return !(a == b);
        }
    private:
        friend class status_list;
        iterator(status_list& parent, std::size_t idx) noexcept;
        status_list* parent_ = nullptr;
        std::size_t  idx_ = 0;
        mutable const git_status_entry* entry_ = nullptr;
    };

    GITPP_NODISCARD iterator begin() noexcept;
    GITPP_NODISCARD iterator end()   noexcept;

private:
    unique_status_list sl_;
};

// ---------------------------------------------------------------------------
// index
// ---------------------------------------------------------------------------

class index {
public:
    index() noexcept = default;
    explicit index(repo* owner, git_index* gi) noexcept;
    index(const index&) = delete;
    index& operator=(const index&) = delete;
    index(index&&) noexcept = default;
    index& operator=(index&&) noexcept = default;

    GITPP_NODISCARD tree write_tree();
    void add_by_path(const std::string& path);
    void remove_by_path(const std::string& path);
    void write() const;
    GITPP_NODISCARD git_index*       native()       noexcept { return idx_.get(); }
    GITPP_NODISCARD const git_index* native() const noexcept { return idx_.get(); }

private:
    repo*        owner_ = nullptr;
    unique_index idx_;
};

// ---------------------------------------------------------------------------
// signature
// ---------------------------------------------------------------------------

class signature {
public:
    signature() noexcept = default;
    signature(const std::string& name, const std::string& email);
    signature(const signature& other);
    signature(signature&& other) noexcept = default;
    signature& operator=(const signature& other);
    signature& operator=(signature&& other) noexcept = default;

    GITPP_NODISCARD git_signature*       native()       noexcept { return sig_.get(); }
    GITPP_NODISCARD const git_signature* native() const noexcept { return sig_.get(); }

private:
    unique_signature sig_;
};

// ---------------------------------------------------------------------------
// remote
// ---------------------------------------------------------------------------

class remote {
public:
    remote() noexcept = default;
    explicit remote(git_remote* r) noexcept;
    remote(const remote& other);
    remote(remote&& other) noexcept = default;
    remote& operator=(const remote& other);
    remote& operator=(remote&& other) noexcept = default;

    void push(const git_strarray* refspecs, const git_push_options* opts);
    void fetch(const git_strarray* refspecs, const git_fetch_options* opts, const std::string& reflog_message = {});
    GITPP_NODISCARD git_remote*       native()       noexcept { return rm_.get(); }
    GITPP_NODISCARD const git_remote* native() const noexcept { return rm_.get(); }
    GITPP_NODISCARD std::string name() const noexcept;
    GITPP_NODISCARD std::string url() const noexcept;

private:
    unique_remote rm_;
};

// ---------------------------------------------------------------------------
// revwalk
// ---------------------------------------------------------------------------

class revwalk {
public:
    revwalk() noexcept = default;
    explicit revwalk(git_repository* repo);
    revwalk(const revwalk&) = delete;
    revwalk& operator=(const revwalk&) = delete;
    revwalk(revwalk&&) noexcept = default;
    revwalk& operator=(revwalk&&) noexcept = default;

    void push_head();
    void push_oid(const oid& o);
    void push_ref(const std::string& refname);
    void hide_oid(const oid& o);
    void sorting(unsigned int sort_mode) const noexcept;
    GITPP_NODISCARD oid next();
    GITPP_NODISCARD git_revwalk* native() noexcept { return walk_.get(); }

private:
    unique_revwalk walk_;
};

// ---------------------------------------------------------------------------
// branch
// ---------------------------------------------------------------------------

class branch {
public:
    branch() noexcept = default;
    GITPP_NODISCARD std::string_view name()          const noexcept;
    GITPP_NODISCARD std::string_view shorthand()     const noexcept;
    GITPP_NODISCARD reference       get_reference()  const;
    GITPP_NODISCARD bool            is_head()        const noexcept;
    GITPP_NODISCARD bool            is_checked_out() const noexcept;
    GITPP_NODISCARD explicit operator bool() const noexcept { return ref_ != nullptr; }

private:
    friend class repo;
    friend class branch_iterator;
    explicit branch(git_reference* ref) noexcept;
    unique_reference ref_;
};

// ---------------------------------------------------------------------------
// branch_iterator
// ---------------------------------------------------------------------------

class branch_iterator {
public:
    branch_iterator() noexcept = default;
    branch_iterator(git_repository* repo, git_branch_t type);
    branch_iterator(const branch_iterator&) = delete;
    branch_iterator& operator=(const branch_iterator&) = delete;
    branch_iterator(branch_iterator&&) noexcept = default;
    branch_iterator& operator=(branch_iterator&&) noexcept = default;

    GITPP_NODISCARD branch next();

private:
    unique_branch_iterator iter_;
};

// ---------------------------------------------------------------------------
// tag
// ---------------------------------------------------------------------------

class tag {
public:
    tag() noexcept = default;
    explicit tag(git_tag* t) noexcept;
    tag(const tag&) = delete;
    tag& operator=(const tag&) = delete;
    tag(tag&&) noexcept = default;
    tag& operator=(tag&&) noexcept = default;

    GITPP_NODISCARD std::string_view name()        const noexcept;
    GITPP_NODISCARD std::string_view message()     const noexcept;
    GITPP_NODISCARD oid             target_id()    const;
    GITPP_NODISCARD git_object_t    target_type()  const noexcept;
    GITPP_NODISCARD explicit operator bool() const noexcept { return tag_ != nullptr; }
    GITPP_NODISCARD git_tag*       native()       noexcept { return tag_.get(); }
    GITPP_NODISCARD const git_tag* native() const noexcept { return tag_.get(); }

private:
    unique_tag tag_;
};

// ---------------------------------------------------------------------------
// diff
// ---------------------------------------------------------------------------

class diff {
public:
    diff() noexcept = default;
    explicit diff(git_diff* d) noexcept;
    diff(const diff&) = delete;
    diff& operator=(const diff&) = delete;
    diff(diff&&) noexcept = default;
    diff& operator=(diff&&) noexcept = default;

    GITPP_NODISCARD std::size_t num_deltas() const noexcept;
    GITPP_NODISCARD const git_diff_delta* get_delta(std::size_t idx) const noexcept;
    GITPP_NODISCARD std::string to_string() const;
    GITPP_NODISCARD explicit operator bool() const noexcept { return diff_ != nullptr; }
    GITPP_NODISCARD git_diff*       native()       noexcept { return diff_.get(); }
    GITPP_NODISCARD const git_diff* native() const noexcept { return diff_.get(); }

private:
    unique_diff diff_;
};

// ---------------------------------------------------------------------------
// repo -- the main repository handle
// ---------------------------------------------------------------------------

class repo {
public:
    repo() noexcept = default;
    explicit repo(const std::filesystem::path& repo_dir);
    repo(const repo&) = delete;
    repo& operator=(const repo&) = delete;
    repo(repo&&) noexcept = default;
    repo& operator=(repo&&) noexcept = default;

    GITPP_NODISCARD git_repository*       native()       noexcept { return repo_.get(); }
    GITPP_NODISCARD const git_repository* native() const noexcept { return repo_.get(); }

    // index & status
    GITPP_NODISCARD index       get_index();
    GITPP_NODISCARD status_list new_status_list();
    void        index_add_bypath(const std::string& path);

    // references
    GITPP_NODISCARD reference head() const;
    GITPP_NODISCARD reference lookup_reference(const std::string& name) const;

    // remotes
    GITPP_NODISCARD remote get_remote(const std::string& remote_name);

    // commits
    GITPP_NODISCARD commit lookup_commit(const oid& commit_id);
    GITPP_NODISCARD commit create_commit(const std::string& update_ref,
                                          const signature& author,
                                          const signature& committer,
                                          const std::string& message,
                                          const tree& tree_obj,
                                          const commit& parent);
    GITPP_NODISCARD commit create_commit(const std::string& update_ref,
                                          const signature& author,
                                          const signature& committer,
                                          const std::string& message,
                                          const tree& tree_obj,
                                          std::vector<commit> parents);

    // trees & blobs
    GITPP_NODISCARD tree  get_tree_by_commit(const oid& commit_id);
    GITPP_NODISCARD tree  get_tree_by_treeid(const oid& tree_oid);
    GITPP_NODISCARD blob  get_blob(const oid& blob_id) const;

    // revwalk
    GITPP_NODISCARD revwalk new_revwalk();

    // branches
    GITPP_NODISCARD branch          lookup_branch(const std::string& name,
                                                   git_branch_t type = GIT_BRANCH_LOCAL);
    GITPP_NODISCARD branch_iterator new_branch_iterator(git_branch_t type = GIT_BRANCH_LOCAL);

    // tags
    GITPP_NODISCARD tag lookup_tag(const oid& tag_id);

    // diff
    GITPP_NODISCARD diff diff_trees(const tree& old_tree, const tree& new_tree);
    GITPP_NODISCARD diff diff_tree_to_workdir(const tree& old_tree);
    GITPP_NODISCARD diff diff_index_to_workdir();

    // properties
    GITPP_NODISCARD bool        is_bare()          const noexcept;
    GITPP_NODISCARD bool        is_empty()         const;
    GITPP_NODISCARD bool        is_head_detached() const;
    GITPP_NODISCARD bool        is_head_unborn()   const;
    GITPP_NODISCARD std::string path()             const;
    GITPP_NODISCARD std::string workdir()          const;

private:
    friend repo init_repo(const std::filesystem::path&, const std::string&, bool);
    explicit repo(git_repository* r) noexcept : repo_(r) {}
    unique_repository repo_;
    void check() const;
};

// ---------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------

GITPP_NODISCARD bool is_git_repo(const std::filesystem::path& dir);
GITPP_NODISCARD repo init_repo(const std::filesystem::path& repo_path,
                                const std::string& url = {},
                                bool bare = false);

} // namespace gitpp
