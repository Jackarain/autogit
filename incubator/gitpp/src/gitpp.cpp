\
// ============================================================================
// gitpp  --  基于 libgit2 的现代 C++20 封装（实现）
// ============================================================================

#include "gitpp/gitpp.hpp"

#include <cstdio>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace gitpp {

// ============================================================================
// libgit2 自动初始化（线程安全，引用计数）
// ============================================================================

namespace {

struct auto_init_libgit2 {
    auto_init_libgit2()  noexcept { git_libgit2_init(); }
    ~auto_init_libgit2() noexcept { git_libgit2_shutdown(); }
};

const auto_init_libgit2 s_ensure_init;

} // 匿名命名空间

// ============================================================================
// exception::error（异常::错误）
// ============================================================================

#if GITPP_HAS_SOURCE_LOCATION

exception::error::error(std::source_location loc) noexcept
    : code_(-1)
    , loc_(std::move(loc))
{
    try {
        const git_error* e = git_error_last();
        msg_ = (e && e->message) ? e->message : "unknown libgit2 error";
    } catch (...) {
        msg_ = "unknown libgit2 error (exception during construction)";
    }
}

exception::error::error(int code, std::source_location loc) noexcept
    : code_(code)
    , loc_(std::move(loc))
{
    try {
        const git_error* e = git_error_last();
        msg_ = (e && e->message) ? e->message : "unknown libgit2 error";
    } catch (...) {
        msg_ = "unknown libgit2 error (exception during construction)";
    }
}

[[noreturn]] void exception::throw_error(int code, std::source_location loc) {
    throw exception::error(code, std::move(loc));
}

#else

exception::error::error(int code) noexcept
    : code_(code)
{
    try {
        const git_error* e = git_error_last();
        msg_ = (e && e->message) ? e->message : "unknown libgit2 error";
    } catch (...) {
        msg_ = "unknown libgit2 error (exception during construction)";
    }
}

[[noreturn]] void exception::throw_error(int code) {
    throw exception::error(code);
}

#endif

// ============================================================================
// oid（对象 ID）
// ============================================================================

std::string oid::to_string() const {
    std::string ret(hex_size, '\0');
    git_oid_tostr(ret.data(), ret.size() + 1, &oid_);
    return ret;
}

oid oid::from_string(std::string_view hex) {
    git_oid out;
    git_oid_fromstrn(&out, hex.data(), hex.size());
    return oid(&out);
}

// ============================================================================
// reference（引用）
// ============================================================================

reference::reference(git_reference* ref) noexcept
    : ref_(ref)
{
}

git_reference_t reference::type() const noexcept {
    return ref_ ? git_reference_type(ref_.get()) : GIT_REFERENCE_INVALID;
}

reference reference::resolve() const {
    if (!ref_)
        throw exception::resolve_failed();
    git_reference* out = nullptr;
    exception::throw_on_error(git_reference_resolve(&out, ref_.get()));
    return reference(out);
}

oid reference::target() const {
    if (!ref_)
        throw exception::resolve_failed();

    if (git_reference_type(ref_.get()) == GIT_REFERENCE_DIRECT) {
        const git_oid* tgt = git_reference_target(ref_.get());
        if (!tgt)
            throw exception::resolve_failed();
        return oid(tgt);
    }
    return resolve().target();
}

std::string reference::name() const {
    return ref_ ? std::string(git_reference_name(ref_.get())) : std::string{};
}

std::string reference::shorthand() const {
    return ref_ ? std::string(git_reference_shorthand(ref_.get())) : std::string{};
}

// ============================================================================
// object（对象）
// ============================================================================

oid object::id() const noexcept {
    return obj_ ? oid(git_object_id(obj_.get())) : oid{};
}

git_object_t object::type() const noexcept {
    return obj_ ? git_object_type(obj_.get()) : GIT_OBJECT_ANY;
}

// ============================================================================
// blob（数据对象）
// ============================================================================

blob::blob(git_blob* b) noexcept
    : object(reinterpret_cast<git_object*>(b))
{
}

std::span<const std::byte> blob::content() const noexcept {
    const auto* raw = git_blob_rawcontent(
        reinterpret_cast<const git_blob*>(obj_.get()));
    return std::span<const std::byte>(
        static_cast<const std::byte*>(raw), size());
}

std::string_view blob::text() const noexcept {
    const auto* raw = git_blob_rawcontent(
        reinterpret_cast<const git_blob*>(obj_.get()));
    return raw ? std::string_view(static_cast<const char*>(raw), size())
               : std::string_view{};
}

std::size_t blob::size() const noexcept {
    return git_blob_rawsize(
        reinterpret_cast<const git_blob*>(obj_.get()));
}

const git_blob* blob::native_blob() const noexcept {
    return reinterpret_cast<const git_blob*>(obj_.get());
}

git_blob* blob::native_blob() noexcept {
    return reinterpret_cast<git_blob*>(obj_.get());
}

// ============================================================================
// commit（提交）
// ============================================================================

commit::commit(git_commit* c) noexcept
    : object(reinterpret_cast<git_object*>(c))
{
}

std::string_view commit::message() const noexcept {
    const auto* raw = git_commit_message(
        reinterpret_cast<const git_commit*>(obj_.get()));
    return raw ? std::string_view(raw) : std::string_view{};
}

oid commit::tree_id() const {
    return oid(git_commit_tree_id(
        reinterpret_cast<const git_commit*>(obj_.get())));
}

git_time_t commit::time() const noexcept {
    return git_commit_time(
        reinterpret_cast<const git_commit*>(obj_.get()));
}

std::string commit::author_name() const {
    const auto* sig = git_commit_author(
        reinterpret_cast<const git_commit*>(obj_.get()));
    return sig ? std::string(sig->name) : std::string{};
}

std::string commit::committer_name() const {
    const auto* sig = git_commit_committer(
        reinterpret_cast<const git_commit*>(obj_.get()));
    return sig ? std::string(sig->name) : std::string{};
}

const git_commit* commit::native_commit() const noexcept {
    return reinterpret_cast<const git_commit*>(obj_.get());
}

git_commit* commit::native_commit() noexcept {
    return reinterpret_cast<git_commit*>(obj_.get());
}

// ============================================================================
// tree_entry（树条目）
// ============================================================================

tree_entry::tree_entry(const git_tree_entry* entry) noexcept
    : entry_(const_cast<git_tree_entry*>(entry)), owned_(false)
{
}

tree_entry::tree_entry(git_tree_entry* entry) noexcept
    : entry_(entry), owned_(true)
{
}

tree_entry::~tree_entry() noexcept {
    reset();
}

tree_entry::tree_entry(const tree_entry& other) noexcept
    : entry_(other.entry_), owned_(false)
{
}

tree_entry::tree_entry(tree_entry&& other) noexcept
    : entry_(std::exchange(other.entry_, nullptr))
    , owned_(std::exchange(other.owned_, false))
{
}

tree_entry& tree_entry::operator=(const tree_entry& other) noexcept {
    if (this != &other) {
        reset();
        entry_ = other.entry_;
        owned_ = false;
    }
    return *this;
}

tree_entry& tree_entry::operator=(tree_entry&& other) noexcept {
    if (this != &other) {
        reset();
        entry_ = std::exchange(other.entry_, nullptr);
        owned_ = std::exchange(other.owned_, false);
    }
    return *this;
}

void tree_entry::reset() noexcept {
    if (owned_ && entry_)
        git_tree_entry_free(entry_);
    entry_ = nullptr;
    owned_ = false;
}

oid tree_entry::id() const {
    if (!entry_)
        return {};
    return oid(git_tree_entry_id(entry_));
}

git_object_t tree_entry::type() const noexcept {
    return entry_ ? git_tree_entry_type(entry_) : GIT_OBJECT_ANY;
}

std::string_view tree_entry::name() const noexcept {
    return entry_ ? std::string_view(git_tree_entry_name(entry_))
                  : std::string_view{};
}

git_filemode_t tree_entry::filemode() const noexcept {
    return entry_ ? git_tree_entry_filemode(entry_) : static_cast<git_filemode_t>(0);
}

// ============================================================================
// tree（树）
// ============================================================================

tree::tree(git_tree* t) noexcept
    : object(reinterpret_cast<git_object*>(t))
{
}

tree_entry tree::by_path(std::string_view path) const {
    git_tree_entry* entry = nullptr;
    exception::throw_on_error(
        git_tree_entry_bypath(&entry, native_tree(),
                              std::string(path).c_str()));
    return tree_entry(entry);
}

tree_entry tree::by_index(std::size_t idx) const noexcept {
    return tree_entry(git_tree_entry_byindex(native_tree(), idx));
}

std::size_t tree::count() const noexcept {
    return git_tree_entrycount(native_tree());
}

const git_tree* tree::native_tree() const noexcept {
    return reinterpret_cast<const git_tree*>(obj_.get());
}

git_tree* tree::native_tree() noexcept {
    return reinterpret_cast<git_tree*>(obj_.get());
}

// --- tree::iterator（树迭代器）-------------------------------------------------------

tree::iterator::iterator(const tree* parent, std::size_t index) noexcept
    : parent_(parent), index_(index)
{
}

tree_entry tree::iterator::operator*() const noexcept {
    if (!entry_ || !static_cast<bool>(entry_))
        entry_ = tree_entry(git_tree_entry_byindex(parent_->native_tree(), index_));
    return entry_;
}

tree::iterator& tree::iterator::operator++() noexcept {
    ++index_;
    entry_ = tree_entry{}; // 使缓存的条目失效
    return *this;
}

tree::iterator tree::begin() const noexcept { return iterator(this, 0); }
tree::iterator tree::end() const noexcept { return iterator(this, count()); }

// ============================================================================
// status_list（状态列表）
// ============================================================================

status_list::status_list(git_status_list* sl) noexcept
    : sl_(sl)
{
}

std::size_t status_list::size() const noexcept {
    return sl_ ? git_status_list_entrycount(sl_.get()) : 0;
}

status_list::iterator status_list::begin() noexcept {
    return iterator(*this, 0);
}

status_list::iterator status_list::end() noexcept {
    return iterator(*this, size());
}

// --- status_list::iterator（状态列表迭代器）------------------------------------------------

status_list::iterator::iterator(status_list& parent, std::size_t idx) noexcept
    : parent_(&parent), idx_(idx)
{
}

status_list::iterator::reference status_list::iterator::operator*() const noexcept {
    entry_ = git_status_byindex(parent_->native(), idx_);
    return entry_;
}

status_list::iterator& status_list::iterator::operator++() noexcept {
    ++idx_;
    entry_ = nullptr;
    return *this;
}

// ============================================================================
// index（索引）
// ============================================================================

index::index(repo* owner, git_index* gi) noexcept
    : owner_(owner), idx_(gi)
{
}

tree index::write_tree() {
    if (!idx_)
        throw std::logic_error("index::write_tree: null index");
    git_oid tree_id;
    exception::throw_on_error(git_index_write_tree(&tree_id, idx_.get()));
    // 我们需要仓库来查找树对象 -- 通过 owner_ 存储
    // 这种方法匹配现有的使用模式。
    // 如果 owner_ 为空，则会失败。
    if (!owner_)
        throw std::logic_error("index::write_tree: no owner repo");
    return owner_->get_tree_by_treeid(oid(&tree_id));
}

void index::add_by_path(const std::string& path) {
    if (!idx_)
        throw std::logic_error("index::add_by_path: null index");
    exception::throw_on_error(git_index_add_bypath(idx_.get(), path.c_str()));
}

void index::remove_by_path(const std::string& path) {
    if (!idx_)
        throw std::logic_error("index::remove_by_path: null index");
    exception::throw_on_error(git_index_remove_bypath(idx_.get(), path.c_str()));
}

void index::write() const {
    if (!idx_)
        throw std::logic_error("index::write: null index");
    exception::throw_on_error(git_index_write(idx_.get()));
}

// ============================================================================
// signature（签名）
// ============================================================================

signature::signature(const std::string& name, const std::string& email) {
    git_signature* sig = nullptr;
    exception::throw_on_error(git_signature_now(&sig, name.c_str(), email.c_str()));
    sig_.reset(sig);
}

signature::signature(const signature& other) {
    if (other.sig_) {
        git_signature* dup = nullptr;
        exception::throw_on_error(git_signature_dup(&dup, other.sig_.get()));
        sig_.reset(dup);
    }
}

signature& signature::operator=(const signature& other) {
    if (this != &other) {
        if (other.sig_) {
            git_signature* dup = nullptr;
            exception::throw_on_error(git_signature_dup(&dup, other.sig_.get()));
            sig_.reset(dup);
        } else {
            sig_.reset();
        }
    }
    return *this;
}

// ============================================================================
// remote（远程）
// ============================================================================

remote::remote(git_remote* r) noexcept
    : rm_(r)
{
}

remote::remote(const remote& other) {
    if (other.rm_) {
        git_remote* dup = nullptr;
        exception::throw_on_error(git_remote_dup(&dup, other.rm_.get()));
        rm_.reset(dup);
    }
}

remote& remote::operator=(const remote& other) {
    if (this != &other) {
        if (other.rm_) {
            git_remote* dup = nullptr;
            exception::throw_on_error(git_remote_dup(&dup, other.rm_.get()));
            rm_.reset(dup);
        } else {
            rm_.reset();
        }
    }
    return *this;
}

void remote::push(const git_strarray* refspecs, const git_push_options* opts) {
    if (!rm_)
        throw std::logic_error("remote::push: null remote");
    exception::throw_on_error(git_remote_push(rm_.get(), refspecs, opts));
}

void remote::fetch(const git_strarray* refspecs, const git_fetch_options* opts,
                   const std::string& reflog_message) {
    if (!rm_)
        throw std::logic_error("remote::fetch: null remote");
    exception::throw_on_error(
        git_remote_fetch(rm_.get(), refspecs, opts,
                         reflog_message.empty() ? nullptr : reflog_message.c_str()));
}

std::string remote::name() const noexcept {
    return rm_ ? std::string(git_remote_name(rm_.get())) : std::string{};
}

std::string remote::url() const noexcept {
    return rm_ ? std::string(git_remote_url(rm_.get())) : std::string{};
}

// ============================================================================
// revwalk（修订遍历器）
// ============================================================================

revwalk::revwalk(git_repository* repo) {
    git_revwalk* walk = nullptr;
    exception::throw_on_error(git_revwalk_new(&walk, repo));
    walk_.reset(walk);
}

void revwalk::push_head() {
    exception::throw_on_error(git_revwalk_push_head(walk_.get()));
}

void revwalk::push_oid(const oid& o) {
    exception::throw_on_error(git_revwalk_push(walk_.get(), o.native()));
}

void revwalk::push_ref(const std::string& refname) {
    exception::throw_on_error(git_revwalk_push_ref(walk_.get(), refname.c_str()));
}

void revwalk::hide_oid(const oid& o) {
    exception::throw_on_error(git_revwalk_hide(walk_.get(), o.native()));
}

void revwalk::sorting(unsigned int sort_mode) const noexcept {
    git_revwalk_sorting(walk_.get(), sort_mode);
}

oid revwalk::next() {
    git_oid out;
    int err = git_revwalk_next(&out, walk_.get());
    if (err == GIT_ITEROVER)
        return {};  // 返回空 oid 表示遍历结束
    if (err < 0)
        exception::throw_on_error(err);
    return oid(&out);
}

// ============================================================================
// branch（分支）
// ============================================================================

branch::branch(git_reference* ref) noexcept
    : ref_(ref)
{
}

std::string_view branch::name() const noexcept {
    if (!ref_)
        return {};
    const char* n = nullptr;
    git_branch_name(&n, ref_.get());
    return n ? std::string_view(n) : std::string_view{};
}

std::string_view branch::shorthand() const noexcept {
    return ref_ ? std::string_view(git_reference_shorthand(ref_.get()))
                : std::string_view{};
}

reference branch::get_reference() const {
    if (!ref_)
        throw std::logic_error("branch::get_reference: invalid branch");
    git_reference* dup = nullptr;
    exception::throw_on_error(git_reference_dup(&dup, ref_.get()));
    return reference(dup);
}

bool branch::is_head() const noexcept {
    return ref_ ? git_branch_is_head(ref_.get()) != 0 : false;
}

bool branch::is_checked_out() const noexcept {
    return ref_ ? git_branch_is_checked_out(ref_.get()) != 0 : false;
}

// ============================================================================
// branch_iterator（分支迭代器）
// ============================================================================

branch_iterator::branch_iterator(git_repository* repo, git_branch_t type) {
    git_branch_iterator* iter = nullptr;
    exception::throw_on_error(git_branch_iterator_new(&iter, repo, type));
    iter_.reset(iter);
}

branch branch_iterator::next() {
    git_reference* out = nullptr;
    git_branch_t type = GIT_BRANCH_LOCAL;
    int err = git_branch_next(&out, &type, iter_.get());
    if (err == GIT_ITEROVER)
        return {};
    if (err < 0)
        exception::throw_on_error(err);
    return branch(out);
}

// ============================================================================
// tag（标签）
// ============================================================================

tag::tag(git_tag* t) noexcept
    : tag_(t)
{
}

std::string_view tag::name() const noexcept {
    return tag_ ? std::string_view(git_tag_name(tag_.get())) : std::string_view{};
}

std::string_view tag::message() const noexcept {
    const auto* msg = tag_ ? git_tag_message(tag_.get()) : nullptr;
    return msg ? std::string_view(msg) : std::string_view{};
}

oid tag::target_id() const {
    if (!tag_)
        return {};
    return oid(git_tag_target_id(tag_.get()));
}

git_object_t tag::target_type() const noexcept {
    return tag_ ? git_tag_target_type(tag_.get()) : GIT_OBJECT_ANY;
}

// ============================================================================
// diff（差异）
// ============================================================================

diff::diff(git_diff* d) noexcept
    : diff_(d)
{
}

std::size_t diff::num_deltas() const noexcept {
    return diff_ ? git_diff_num_deltas(diff_.get()) : 0;
}

const git_diff_delta* diff::get_delta(std::size_t idx) const noexcept {
    return diff_ ? git_diff_get_delta(diff_.get(), idx) : nullptr;
}

std::string diff::to_string() const {
    if (!diff_)
        return {};

    std::ostringstream oss;
    for (std::size_t i = 0; i < num_deltas(); ++i) {
        const auto* delta = get_delta(i);
        if (!delta) continue;
        oss << (delta->status == GIT_DELTA_ADDED ? "A " :
                delta->status == GIT_DELTA_DELETED ? "D " :
                delta->status == GIT_DELTA_MODIFIED ? "M " :
                delta->status == GIT_DELTA_RENAMED ? "R " :
                delta->status == GIT_DELTA_COPIED ? "C " : "? ")
            << (delta->old_file.path ? delta->old_file.path : "(null)")
            << " -> "
            << (delta->new_file.path ? delta->new_file.path : "(null)")
            << '\n';
    }
    return oss.str();
}

// ============================================================================
// repo（仓库）
// ============================================================================

repo::repo(const std::filesystem::path& repo_dir) {
    git_repository* r = nullptr;
    int err = git_repository_open_ext(
        &r,
        repo_dir.string().c_str(),
        GIT_REPOSITORY_OPEN_NO_SEARCH,
        nullptr);
    if (err != 0)
        throw exception::not_repo();
    repo_.reset(r);
}

void repo::check() const {
    if (!repo_)
        throw std::logic_error("repo: null repository handle");
}

// --- 属性（properties）-----------------------------------------------------------

bool repo::is_bare() const noexcept {
    return repo_ ? git_repository_is_bare(repo_.get()) != 0 : false;
}

bool repo::is_empty() const {
    check();
    return git_repository_is_empty(repo_.get()) != 0;
}

bool repo::is_head_detached() const {
    check();
    return git_repository_head_detached(repo_.get()) != 0;
}

bool repo::is_head_unborn() const {
    check();
    return git_repository_head_unborn(repo_.get()) != 0;
}

std::string repo::path() const {
    check();
    return git_repository_path(repo_.get());
}

std::string repo::workdir() const {
    check();
    const auto* wd = git_repository_workdir(repo_.get());
    return wd ? std::string(wd) : std::string{};
}

// --- 索引与状态（index & status）-------------------------------------------------------

index repo::get_index() {
    check();
    git_index* gi = nullptr;
    exception::throw_on_error(git_repository_index(&gi, repo_.get()));
    return index(this, gi);
}

status_list repo::new_status_list() {
    check();
    git_status_list* sl = nullptr;
    exception::throw_on_error(git_status_list_new(&sl, repo_.get(), nullptr));
    return status_list(sl);
}

void repo::index_add_bypath(const std::string& path) {
    check();
    auto idx = get_index();
    idx.add_by_path(path);
    idx.write();
}

// --- 引用（references）-----------------------------------------------------------

reference repo::head() const {
    check();
    git_reference* out = nullptr;
    exception::throw_on_error(git_repository_head(&out, repo_.get()));
    return reference(out);
}

reference repo::lookup_reference(const std::string& name) const {
    check();
    git_reference* out = nullptr;
    exception::throw_on_error(git_reference_lookup(&out, repo_.get(), name.c_str()));
    return reference(out);
}

// --- 远程（remotes）--------------------------------------------------------------

remote repo::get_remote(const std::string& remote_name) {
    check();
    git_remote* gr = nullptr;
    exception::throw_on_error(git_remote_lookup(&gr, repo_.get(), remote_name.c_str()));
    return remote(gr);
}

// --- 提交（commits）--------------------------------------------------------------

commit repo::lookup_commit(const oid& commit_id) {
    check();
    git_commit* c = nullptr;
    exception::throw_on_error(git_commit_lookup(&c, repo_.get(), commit_id.native()));
    return commit(c);
}

commit repo::create_commit(const std::string& update_ref,
                            const signature& author,
                            const signature& committer,
                            const std::string& message,
                            const tree& tree_obj,
                            const commit& parent) {
    check();
    git_oid commit_id;
    const git_commit* parents_array[1] = { parent.native_commit() };

    exception::throw_on_error(git_commit_create(
        &commit_id,
        repo_.get(),
        update_ref.empty() ? nullptr : update_ref.c_str(),
        author.native(),
        committer.native(),
        nullptr,                    // 编码
        message.c_str(),
        tree_obj.native_tree(),
        1,                          // 父提交数量
        parents_array));

    return lookup_commit(oid(&commit_id));
}

commit repo::create_commit(const std::string& update_ref,
                            const signature& author,
                            const signature& committer,
                            const std::string& message,
                            const tree& tree_obj,
                            std::vector<commit> parents) {
    check();
    git_oid commit_id;

    std::vector<const git_commit*> parents_array;
    parents_array.reserve(parents.size());
    std::transform(parents.begin(), parents.end(),
                   std::back_inserter(parents_array),
                   [](const commit& p) { return p.native_commit(); });

    exception::throw_on_error(git_commit_create(
        &commit_id,
        repo_.get(),
        update_ref.empty() ? nullptr : update_ref.c_str(),
        author.native(),
        committer.native(),
        nullptr,
        message.c_str(),
        tree_obj.native_tree(),
        static_cast<int>(parents.size()),
        parents_array.data()));

    return lookup_commit(oid(&commit_id));
}

// --- 树与数据对象（trees & blobs）--------------------------------------------------------

tree repo::get_tree_by_commit(const oid& commit_id) {
    check();
    git_commit* c = nullptr;
    exception::throw_on_error(git_commit_lookup(&c, repo_.get(), commit_id.native()));

    git_tree* t = nullptr;
    int err = git_commit_tree(&t, c);
    git_commit_free(c);
    exception::throw_on_error(err);

    return tree(t);
}

tree repo::get_tree_by_treeid(const oid& tree_oid) {
    check();
    git_tree* t = nullptr;
    exception::throw_on_error(git_tree_lookup(&t, repo_.get(), tree_oid.native()));
    return tree(t);
}

blob repo::get_blob(const oid& blob_id) const {
    check();
    git_blob* b = nullptr;
    exception::throw_on_error(git_blob_lookup(&b, repo_.get(), blob_id.native()));
    return blob(b);
}

// --- 修订遍历器（revwalk）--------------------------------------------------------------

revwalk repo::new_revwalk() {
    check();
    return revwalk(repo_.get());
}

// --- 分支（branches）-------------------------------------------------------------

branch repo::lookup_branch(const std::string& name, git_branch_t type) {
    check();
    git_reference* out = nullptr;
    exception::throw_on_error(git_branch_lookup(&out, repo_.get(), name.c_str(), type));
    return branch(out);
}

branch_iterator repo::new_branch_iterator(git_branch_t type) {
    check();
    return branch_iterator(repo_.get(), type);
}

// --- 标签（tags）-----------------------------------------------------------------

tag repo::lookup_tag(const oid& tag_id) {
    check();
    git_tag* t = nullptr;
    exception::throw_on_error(git_tag_lookup(&t, repo_.get(), tag_id.native()));
    return tag(t);
}

// --- 差异（diff）-----------------------------------------------------------------

diff repo::diff_trees(const tree& old_tree, const tree& new_tree) {
    check();
    git_diff* d = nullptr;
    exception::throw_on_error(git_diff_tree_to_tree(
        &d, repo_.get(),
        const_cast<git_tree*>(old_tree.native_tree()),
        const_cast<git_tree*>(new_tree.native_tree()),
        nullptr));
    return diff(d);
}

diff repo::diff_tree_to_workdir(const tree& old_tree) {
    check();
    git_diff* d = nullptr;
    exception::throw_on_error(git_diff_tree_to_workdir(
        &d, repo_.get(),
        const_cast<git_tree*>(old_tree.native_tree()),
        nullptr));
    return diff(d);
}

diff repo::diff_index_to_workdir() {
    check();
    git_diff* d = nullptr;
    exception::throw_on_error(git_diff_index_to_workdir(
        &d, repo_.get(), nullptr, nullptr));
    return diff(d);
}

// ============================================================================
// 自由函数（Free functions）
// ============================================================================

bool is_git_repo(const std::filesystem::path& dir) {
    git_repository* r = nullptr;
    int err = git_repository_open_ext(
        &r,
        dir.string().c_str(),
        GIT_REPOSITORY_OPEN_NO_SEARCH,
        nullptr);
    if (err == 0)
        git_repository_free(r);
    return err == 0;
}

repo init_repo(const std::filesystem::path& repo_path,
               const std::string& url,
               bool bare) {
    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.flags = GIT_REPOSITORY_INIT_MKPATH;
    if (bare)
        opts.flags |= GIT_REPOSITORY_INIT_BARE;
    opts.origin_url = url.empty() ? nullptr : url.c_str();
    opts.initial_head = "master";

    git_repository* r = nullptr;
    int ret = git_repository_init_ext(&r, repo_path.string().c_str(), &opts);
    if (ret != 0 || !r) {
        if (r) git_repository_free(r);
        exception::throw_on_error(ret);
    }
    return repo(r);
}

} // namespace gitpp（gitpp 命名空间）
