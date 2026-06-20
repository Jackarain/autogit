#include "gitpp/gitpp.hpp"

#include <git2.h>

#include <cstring>
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace gitpp {

// ==========================================================================
//  Auto‑initialisation of libgit2 (thread‑safe ref‑counted)
// ==========================================================================

namespace {

	struct auto_init_libgit2 {
		auto_init_libgit2()  noexcept { git_libgit2_init(); }
		~auto_init_libgit2() noexcept { git_libgit2_shutdown(); }
	};

	const auto_init_libgit2 s_ensure_init;

} // anonymous namespace

// ==========================================================================
//  exception::error
// ==========================================================================

exception::error::error()
{
	const git_error* e = git_error_last();
	if (e && e->message)
		msg_ = e->message;
	else
		msg_ = "unknown libgit2 error";
}

// ==========================================================================
//  oid
// ==========================================================================

bool oid::operator==(const oid& other) const noexcept
{
	return git_oid_equal(&oid_, &other.oid_) != 0;
}

oid& oid::operator=(const oid& other) noexcept
{
	git_oid_cpy(&oid_, &other.oid_);
	return *this;
}

bool oid::is_zero() const noexcept
{
	return git_oid_is_zero(&oid_) != 0;
}

std::string oid::as_sha1_string() const
{
	std::string ret(GIT_OID_SHA1_HEXSIZE, '\0');
	git_oid_tostr(ret.data(), ret.size() + 1, &oid_);
	return ret;
}

oid oid::from_sha1_string(std::string_view s)
{
	git_oid out;
	git_oid_fromstrn(&out, s.data(), s.size());
	return oid(&out);
}

// ==========================================================================
//  reference
// ==========================================================================

reference::reference(git_reference* ref) noexcept
	: ref_(ref), owned_(true)
{
}

reference::reference(const git_reference* ref) noexcept
	: ref_(const_cast<git_reference*>(ref)), owned_(false)
{
}

reference::~reference() noexcept
{
	if (owned_ && ref_)
		git_reference_free(ref_);
}

reference::reference(reference&& other) noexcept
	: ref_(other.ref_), owned_(other.owned_)
{
	other.ref_   = nullptr;
	other.owned_ = false;
}

reference::reference(const reference& other)
	: ref_(nullptr), owned_(true)
{
	if (other.ref_)
		git_reference_dup(&ref_, other.ref_);
}

reference& reference::operator=(reference&& other) noexcept
{
	if (this != &other) {
		if (owned_ && ref_)
			git_reference_free(ref_);
		ref_        = other.ref_;
		owned_      = other.owned_;
		other.ref_  = nullptr;
		other.owned_ = false;
	}
	return *this;
}

reference& reference::operator=(const reference& other)
{
	if (this != &other) {
		git_reference* dup = nullptr;
		if (other.ref_) {
			int err = git_reference_dup(&dup, other.ref_);
			if (err < 0)
				exception::throw_on_error(err);
		}
		if (owned_ && ref_)
			git_reference_free(ref_);
		ref_   = dup;
		owned_ = true;
	}
	return *this;
}

bool reference::operator==(const reference& other) const noexcept
{
	if (ref_ == other.ref_)
		return true;
	if (!ref_ || !other.ref_)
		return false;
	return git_reference_cmp(ref_, other.ref_) == 0;
}

git_reference_t reference::type() const noexcept
{
	return ref_ ? git_reference_type(ref_) : GIT_REFERENCE_INVALID;
}

reference reference::resolve() const
{
	if (!ref_)
		throw exception::resolve_failed();
	git_reference* out = nullptr;
	exception::throw_on_error(git_reference_resolve(&out, ref_));
	return reference(out);
}

oid reference::target() const
{
	if (!ref_)
		throw exception::resolve_failed();

	if (git_reference_type(ref_) == GIT_REFERENCE_DIRECT) {
		const git_oid* tgt = git_reference_target(ref_);
		if (!tgt)
			throw exception::resolve_failed();
		return oid(tgt);
	}

	return resolve().target();
}

std::string reference::name() const
{
	if (!ref_)
		return {};
	return git_reference_name(ref_);
}

std::string reference::shorthand() const
{
	if (!ref_)
		return {};
	return git_reference_shorthand(ref_);
}

// ==========================================================================
//  object
// ==========================================================================

object::object(git_object* o) noexcept
	: obj_(o)
{
}

object::object(const object& o)
{
	if (o.obj_)
		git_object_dup(&obj_, o.obj_);
}

object::object(object&& o) noexcept
	: obj_(o.obj_)
{
	o.obj_ = nullptr;
}

object::~object()
{
	if (obj_)
		git_object_free(obj_);
}

object& object::operator=(object&& o) noexcept
{
	if (this != &o) {
		if (obj_)
			git_object_free(obj_);
		obj_    = o.obj_;
		o.obj_  = nullptr;
	}
	return *this;
}

object& object::operator=(const object& o)
{
	if (this != &o) {
		git_object* dup = nullptr;
		if (o.obj_) {
			int err = git_object_dup(&dup, o.obj_);
			if (err < 0)
				exception::throw_on_error(err);
		}
		if (obj_)
			git_object_free(obj_);
		obj_ = dup;
	}
	return *this;
}

oid object::get_oid() const
{
	if (!obj_)
		return {};
	return oid(git_object_id(obj_));
}

// ==========================================================================
//  blob
// ==========================================================================

blob::blob(git_blob* b) noexcept
	: object(reinterpret_cast<git_object*>(b))
{
}

std::string_view blob::get_content() const noexcept
{
	const auto* content = static_cast<const char*>(
		git_blob_rawcontent(reinterpret_cast<const git_blob*>(obj_)));
	auto content_size = git_blob_rawsize(
		reinterpret_cast<const git_blob*>(obj_));
	return std::string_view(content, content_size);
}

std::size_t blob::size() const noexcept
{
	return git_blob_rawsize(reinterpret_cast<const git_blob*>(obj_));
}

// ==========================================================================
//  commit
// ==========================================================================

commit::commit(git_commit* c) noexcept
	: object(reinterpret_cast<git_object*>(c))
{
}

git_commit* commit::native_handle() noexcept
{
	return reinterpret_cast<git_commit*>(obj_);
}

const git_commit* commit::native_handle() const noexcept
{
	return reinterpret_cast<const git_commit*>(obj_);
}

std::string_view commit::message() const noexcept
{
	const auto* msg = git_commit_message(native_handle());
	return msg ? std::string_view(msg) : std::string_view{};
}

oid commit::tree_id() const
{
	return oid(git_commit_tree_id(native_handle()));
}

git_time_t commit::time() const
{
	return git_commit_time(native_handle());
}

// ==========================================================================
//  tree_entry
// ==========================================================================

tree_entry::tree_entry(const git_tree_entry* entry) noexcept
	: entry_(const_cast<git_tree_entry*>(entry)), owned_(false)
{
}

tree_entry::tree_entry(git_tree_entry* entry) noexcept
	: entry_(entry), owned_(true)
{
}

tree_entry::~tree_entry() noexcept
{
	release();
}

tree_entry::tree_entry(const tree_entry& other) noexcept
	: entry_(other.entry_), owned_(false)
{
}

tree_entry::tree_entry(tree_entry&& other) noexcept
	: entry_(other.entry_), owned_(other.owned_)
{
	other.entry_ = nullptr;
	other.owned_ = false;
}

tree_entry& tree_entry::operator=(const tree_entry& other) noexcept
{
	if (this != &other) {
		release();
		entry_ = other.entry_;
		owned_ = false;
	}
	return *this;
}

tree_entry& tree_entry::operator=(tree_entry&& other) noexcept
{
	if (this != &other) {
		release();
		entry_       = other.entry_;
		owned_       = other.owned_;
		other.entry_ = nullptr;
		other.owned_ = false;
	}
	return *this;
}

void tree_entry::release() noexcept
{
	if (owned_ && entry_)
		git_tree_entry_free(entry_);
	entry_ = nullptr;
	owned_ = false;
}

oid tree_entry::get_oid() const
{
	if (!entry_)
		return {};
	return oid(git_tree_entry_id(entry_));
}

git_object_t tree_entry::type() const noexcept
{
	return entry_ ? git_tree_entry_type(entry_) : GIT_OBJECT_ANY;
}

std::string_view tree_entry::name() const noexcept
{
	return entry_ ? std::string_view(git_tree_entry_name(entry_))
	              : std::string_view{};
}

// ==========================================================================
//  tree
// ==========================================================================

tree::tree(git_tree* t) noexcept
	: object(reinterpret_cast<git_object*>(t))
{
}

const git_tree* tree::native_handle() const noexcept
{
	return reinterpret_cast<const git_tree*>(obj_);
}

git_tree* tree::native_handle() noexcept
{
	return reinterpret_cast<git_tree*>(obj_);
}

tree_entry tree::by_path(std::string_view path) const
{
	git_tree_entry* entry = nullptr;
	int err = git_tree_entry_bypath(&entry, native_handle(),
	                                 std::string(path).c_str());
	if (err < 0)
		exception::throw_on_error(err);
	return tree_entry(entry);
}

tree::tree_iterator tree::begin() const noexcept
{
	return tree_iterator(this, 0);
}

tree::tree_iterator tree::end() const noexcept
{
	return tree_iterator(this, git_tree_entrycount(native_handle()));
}

tree::tree_iterator::tree_iterator(const tree* parent, std::size_t index) noexcept
	: parent_(parent)
	, index_(index)
{
}

tree::tree_iterator& tree::tree_iterator::operator++() noexcept
{
	++index_;
	return *this;
}

tree_entry tree::tree_iterator::operator*() const noexcept
{
	return tree_entry(
		git_tree_entry_byindex(parent_->native_handle(), index_));
}

// ==========================================================================
//  status_list
// ==========================================================================

status_list::status_list(git_status_list* sl) noexcept
	: status_list_(sl)
{
}

status_list::~status_list() noexcept
{
	if (status_list_)
		git_status_list_free(status_list_);
}

git_status_list* status_list::native_handle() noexcept
{
	return status_list_;
}

const git_status_list* status_list::native_handle() const noexcept
{
	return status_list_;
}

status_list::git_status_entry_iterator status_list::begin() noexcept
{
	return git_status_entry_iterator(*this, 0);
}

status_list::git_status_entry_iterator status_list::end() noexcept
{
	return git_status_entry_iterator(*this, size());
}

std::size_t status_list::size() const noexcept
{
	return status_list_ ? git_status_list_entrycount(status_list_) : 0;
}

status_list::git_status_entry_iterator::git_status_entry_iterator(
	status_list& parent, std::size_t idx) noexcept
	: parent_(parent)
	, idx_(idx)
{
}

const git_status_entry*
status_list::git_status_entry_iterator::operator*() const noexcept
{
	return git_status_byindex(parent_.native_handle(), idx_);
}

status_list::git_status_entry_iterator&
status_list::git_status_entry_iterator::operator++() noexcept
{
	++idx_;
	return *this;
}

bool status_list::git_status_entry_iterator::operator==(
	const git_status_entry_iterator& other) const noexcept
{
	return (&parent_ == &other.parent_) && (idx_ == other.idx_);
}

// ==========================================================================
//  index
// ==========================================================================

index::index(repo* belong, git_index* gi) noexcept
	: belong_(belong)
	, index_(gi)
{
}

index::~index() noexcept
{
	if (index_)
		git_index_free(index_);
}

git_index* index::native_handle() noexcept
{
	return index_;
}

tree index::write_tree()
{
	if (!index_)
		throw std::logic_error("index::write_tree: null index");

	git_oid tree_id;
	exception::throw_on_error(git_index_write_tree(&tree_id, index_));
	return belong_->get_tree_by_treeid(oid(&tree_id));
}

void index::add_by_path(const std::string& path)
{
	if (!index_)
		throw std::logic_error("index::add_by_path: null index");
	exception::throw_on_error(git_index_add_bypath(index_, path.c_str()));
}

void index::remove_by_path(const std::string& path)
{
	if (!index_)
		throw std::logic_error("index::remove_by_path: null index");
	exception::throw_on_error(git_index_remove_bypath(index_, path.c_str()));
}

void index::write() const
{
	if (!index_)
		throw std::logic_error("index::write: null index");
	exception::throw_on_error(git_index_write(const_cast<git_index*>(index_)));
}

// ==========================================================================
//  signature
// ==========================================================================

signature::signature(const signature& other)
{
	if (other.git_sig_) {
		int err = git_signature_dup(&git_sig_, other.git_sig_);
		if (err < 0)
			exception::throw_on_error(err);
	}
}

signature::signature(signature&& other) noexcept
	: git_sig_(other.git_sig_)
{
	other.git_sig_ = nullptr;
}

signature::signature(const std::string& name, const std::string& email)
{
	exception::throw_on_error(
		git_signature_now(&git_sig_, name.c_str(), email.c_str()));
}

signature::~signature() noexcept
{
	if (git_sig_)
		git_signature_free(git_sig_);
}

signature& signature::operator=(const signature& other)
{
	if (this != &other) {
		git_signature* dup = nullptr;
		if (other.git_sig_) {
			int err = git_signature_dup(&dup, other.git_sig_);
			if (err < 0)
				exception::throw_on_error(err);
		}
		if (git_sig_)
			git_signature_free(git_sig_);
		git_sig_ = dup;
	}
	return *this;
}

signature& signature::operator=(signature&& other) noexcept
{
	if (this != &other) {
		if (git_sig_)
			git_signature_free(git_sig_);
		git_sig_        = other.git_sig_;
		other.git_sig_  = nullptr;
	}
	return *this;
}

git_signature* signature::native_handle() noexcept
{
	return git_sig_;
}

const git_signature* signature::native_handle() const noexcept
{
	return git_sig_;
}

// ==========================================================================
//  remote
// ==========================================================================

remote::remote(git_remote* r) noexcept
	: git_remote_(r)
{
}

remote::remote(const remote& other)
{
	if (other.git_remote_) {
		int err = git_remote_dup(&git_remote_, other.git_remote_);
		if (err < 0)
			exception::throw_on_error(err);
	}
}

remote::remote(remote&& other) noexcept
	: git_remote_(other.git_remote_)
{
	other.git_remote_ = nullptr;
}

remote::~remote() noexcept
{
	if (git_remote_)
		git_remote_free(git_remote_);
}

remote& remote::operator=(const remote& other)
{
	if (this != &other) {
		git_remote* dup = nullptr;
		if (other.git_remote_) {
			int err = git_remote_dup(&dup, other.git_remote_);
			if (err < 0)
				exception::throw_on_error(err);
		}
		if (git_remote_)
			git_remote_free(git_remote_);
		git_remote_ = dup;
	}
	return *this;
}

remote& remote::operator=(remote&& other) noexcept
{
	if (this != &other) {
		if (git_remote_)
			git_remote_free(git_remote_);
		git_remote_        = other.git_remote_;
		other.git_remote_  = nullptr;
	}
	return *this;
}

git_remote* remote::native_handle() noexcept
{
	return git_remote_;
}

void remote::push(const git_strarray* refspecs, const git_push_options* opts)
{
	if (!git_remote_)
		throw std::logic_error("remote::push: null remote");
	exception::throw_on_error(git_remote_push(git_remote_, refspecs, opts));
}

// ==========================================================================
//  revwalk
// ==========================================================================

revwalk::revwalk(git_repository* repo)
{
	exception::throw_on_error(git_revwalk_new(&walk_, repo));
}

revwalk::~revwalk() noexcept
{
	if (walk_)
		git_revwalk_free(walk_);
}

revwalk::revwalk(revwalk&& other) noexcept
	: walk_(other.walk_)
{
	other.walk_ = nullptr;
}

revwalk& revwalk::operator=(revwalk&& other) noexcept
{
	if (this != &other) {
		if (walk_)
			git_revwalk_free(walk_);
		walk_       = other.walk_;
		other.walk_ = nullptr;
	}
	return *this;
}

void revwalk::push_head()
{
	exception::throw_on_error(git_revwalk_push_head(walk_));
}

void revwalk::push_oid(const oid& o)
{
	exception::throw_on_error(git_revwalk_push(walk_, &o));
}

void revwalk::push_ref(const std::string& refname)
{
	exception::throw_on_error(git_revwalk_push_ref(walk_, refname.c_str()));
}

void revwalk::hide_oid(const oid& o)
{
	exception::throw_on_error(git_revwalk_hide(walk_, &o));
}

void revwalk::sorting(unsigned int sort_mode) const noexcept
{
	git_revwalk_sorting(walk_, sort_mode);
}

oid revwalk::next()
{
	git_oid out;
	int err = git_revwalk_next(&out, walk_);
	if (err == GIT_ITEROVER)
		return {};      // zero oid → end
	if (err < 0)
		exception::throw_on_error(err);
	return oid(&out);
}

// ==========================================================================
//  branch
// ==========================================================================

branch::branch(git_reference* ref) noexcept
	: ref_(ref)
{
}

std::string_view branch::name() const noexcept
{
	if (!ref_)
		return {};
	const char* n = nullptr;
	git_branch_name(&n, ref_);
	return n ? std::string_view(n) : std::string_view{};
}

std::string_view branch::shorthand() const noexcept
{
	if (!ref_)
		return {};
	return std::string_view(git_reference_shorthand(ref_));
}

reference branch::get_reference() const
{
	if (!ref_)
		throw std::logic_error("branch::get_reference: invalid branch");
	git_reference* dup = nullptr;
	exception::throw_on_error(git_reference_dup(&dup, ref_));
	return reference(dup);
}

bool branch::is_head() const noexcept
{
	return ref_ ? git_branch_is_head(ref_) != 0 : false;
}

// -----------------------------------------------------------------------
//  branch_iterator
// -----------------------------------------------------------------------

branch_iterator::branch_iterator(git_repository* repo, git_branch_t type)
{
	exception::throw_on_error(
		git_branch_iterator_new(&iter_, repo, type));
}

branch_iterator::~branch_iterator() noexcept
{
	if (iter_)
		git_branch_iterator_free(iter_);
}

branch_iterator::branch_iterator(branch_iterator&& other) noexcept
	: iter_(other.iter_)
{
	other.iter_ = nullptr;
}

branch_iterator& branch_iterator::operator=(branch_iterator&& other) noexcept
{
	if (this != &other) {
		if (iter_)
			git_branch_iterator_free(iter_);
		iter_       = other.iter_;
		other.iter_ = nullptr;
	}
	return *this;
}

branch branch_iterator::next()
{
	git_reference* out = nullptr;
	git_branch_t type  = GIT_BRANCH_LOCAL;
	int err = git_branch_next(&out, &type, iter_);
	if (err == GIT_ITEROVER)
		return {};
	if (err < 0)
		exception::throw_on_error(err);
	return branch(out);
}

// ==========================================================================
//  tag
// ==========================================================================

tag::tag(git_tag* t) noexcept
	: tag_(t)
{
}

tag::~tag() noexcept
{
	if (tag_)
		git_tag_free(tag_);
}

tag::tag(tag&& other) noexcept
	: tag_(other.tag_)
{
	other.tag_ = nullptr;
}

tag& tag::operator=(tag&& other) noexcept
{
	if (this != &other) {
		if (tag_)
			git_tag_free(tag_);
		tag_        = other.tag_;
		other.tag_  = nullptr;
	}
	return *this;
}

std::string_view tag::name() const noexcept
{
	return tag_ ? std::string_view(git_tag_name(tag_)) : std::string_view{};
}

std::string_view tag::message() const noexcept
{
	const auto* msg = tag_ ? git_tag_message(tag_) : nullptr;
	return msg ? std::string_view(msg) : std::string_view{};
}

oid tag::target_id() const
{
	if (!tag_)
		return {};
	return oid(git_tag_target_id(tag_));
}

git_object_t tag::target_type() const noexcept
{
	return tag_ ? git_tag_target_type(tag_) : GIT_OBJECT_ANY;
}

// ==========================================================================
//  diff
// ==========================================================================

diff::diff(git_diff* d) noexcept
	: diff_(d)
{
}

diff::~diff() noexcept
{
	if (diff_)
		git_diff_free(diff_);
}

diff::diff(diff&& other) noexcept
	: diff_(other.diff_)
{
	other.diff_ = nullptr;
}

diff& diff::operator=(diff&& other) noexcept
{
	if (this != &other) {
		if (diff_)
			git_diff_free(diff_);
		diff_       = other.diff_;
		other.diff_ = nullptr;
	}
	return *this;
}

std::size_t diff::num_deltas() const noexcept
{
	return diff_ ? git_diff_num_deltas(diff_) : 0;
}

const git_diff_delta* diff::get_delta(std::size_t idx) const noexcept
{
	return diff_ ? git_diff_get_delta(diff_, idx) : nullptr;
}

// ==========================================================================
//  repo
// ==========================================================================

repo::repo(git_repository* r) noexcept
	: repo_(r)
{
}

repo::repo(std::filesystem::path repo_dir)
	: repo_(nullptr)
{
	int err = git_repository_open_ext(
		&repo_,
		repo_dir.string().c_str(),
		GIT_REPOSITORY_OPEN_NO_SEARCH,
		nullptr);
	if (err != 0)
		throw exception::not_repo();
}

repo::~repo() noexcept
{
	if (repo_)
		git_repository_free(repo_);
}

repo::repo(repo&& other) noexcept
	: repo_(other.repo_)
{
	other.repo_ = nullptr;
}

repo& repo::operator=(repo&& other) noexcept
{
	if (this != &other) {
		if (repo_)
			git_repository_free(repo_);
		repo_       = other.repo_;
		other.repo_ = nullptr;
	}
	return *this;
}

void repo::check_repo() const
{
	if (!repo_)
		throw std::logic_error("repo: null repository handle");
}

bool repo::is_bare() const noexcept
{
	return repo_ ? git_repository_is_bare(repo_) != 0 : false;
}

bool repo::is_empty() const
{
	check_repo();
	return git_repository_is_empty(repo_) != 0;
}

bool repo::is_head_detached() const
{
	check_repo();
	return git_repository_head_detached(repo_) != 0;
}

bool repo::is_head_unborn() const
{
	check_repo();
	return git_repository_head_unborn(repo_) != 0;
}

std::string repo::path() const
{
	check_repo();
	return git_repository_path(repo_);
}

std::string repo::workdir() const
{
	check_repo();
	const auto* wd = git_repository_workdir(repo_);
	return wd ? std::string(wd) : std::string{};
}

// --- index & status -------------------------------------------------------

index repo::get_index()
{
	check_repo();
	git_index* gi = nullptr;
	exception::throw_on_error(git_repository_index(&gi, repo_));
	return index(this, gi);
}

status_list repo::new_status_list()
{
	check_repo();
	git_status_list* sl = nullptr;
	exception::throw_on_error(git_status_list_new(&sl, repo_, nullptr));
	return status_list(sl);
}

void repo::index_add_bypath(const std::string& path)
{
	check_repo();
	git_index* idx = nullptr;
	exception::throw_on_error(git_repository_index(&idx, repo_));
	exception::throw_on_error(git_index_add_bypath(idx, path.c_str()));
	exception::throw_on_error(git_index_write(idx));
	git_index_free(idx);
}

// --- references -----------------------------------------------------------

reference repo::head() const
{
	check_repo();
	git_reference* out = nullptr;
	exception::throw_on_error(git_repository_head(&out, repo_));
	return reference(out);
}

reference repo::lookup_reference(const std::string& name) const
{
	check_repo();
	git_reference* out = nullptr;
	exception::throw_on_error(git_reference_lookup(&out, repo_, name.c_str()));
	return reference(out);
}

// --- remotes --------------------------------------------------------------

remote repo::get_remote(const std::string& remote_name)
{
	check_repo();
	git_remote* gr = nullptr;
	exception::throw_on_error(
		git_remote_lookup(&gr, repo_, remote_name.c_str()));
	return remote(gr);
}

// --- commits --------------------------------------------------------------

commit repo::lookup_commit(oid commit_id)
{
	check_repo();
	git_commit* c = nullptr;
	exception::throw_on_error(
		git_commit_lookup(&c, repo_, &commit_id));
	return commit(c);
}

commit repo::create_commit(
	const std::string& update_ref,
	const signature& author,
	const signature& committer,
	const std::string& message,
	const tree& tree,
	commit parent)
{
	check_repo();
	git_oid commit_id;
	const git_commit* parents_array[1] = { parent.native_handle() };

	exception::throw_on_error(git_commit_create(
		&commit_id,
		repo_,
		update_ref.empty() ? nullptr : update_ref.c_str(),
		author.native_handle(),
		committer.native_handle(),
		nullptr,        // encoding
		message.c_str(),
		tree.native_handle(),
		1,              // parent count
		parents_array));

	return lookup_commit(oid(&commit_id));
}

commit repo::create_commit(
	const std::string& update_ref,
	const signature& author,
	const signature& committer,
	const std::string& message,
	const tree& tree,
	std::vector<commit> parents)
{
	check_repo();
	git_oid commit_id;

	std::vector<const git_commit*> parents_array;
	parents_array.reserve(parents.size());
	std::transform(parents.begin(), parents.end(),
		std::back_inserter(parents_array),
		[](commit& p) { return p.native_handle(); });

	exception::throw_on_error(git_commit_create(
		&commit_id,
		repo_,
		update_ref.empty() ? nullptr : update_ref.c_str(),
		author.native_handle(),
		committer.native_handle(),
		nullptr,
		message.c_str(),
		tree.native_handle(),
		static_cast<int>(parents.size()),
		parents_array.data()));

	return lookup_commit(oid(&commit_id));
}

// --- trees & blobs --------------------------------------------------------

tree repo::get_tree_by_commit(oid commit_id)
{
	check_repo();
	git_commit* c = nullptr;
	exception::throw_on_error(git_commit_lookup(&c, repo_, &commit_id));

	git_tree* t = nullptr;
	int err = git_commit_tree(&t, c);
	git_commit_free(c);
	exception::throw_on_error(err);

	return tree(t);
}

tree repo::get_tree_by_treeid(oid tree_oid)
{
	check_repo();
	git_tree* t = nullptr;
	exception::throw_on_error(git_tree_lookup(&t, repo_, &tree_oid));
	return tree(t);
}

tree repo::get_tree_from_reference(oid tree_oid)
{
	return get_tree_by_treeid(tree_oid);
}

blob repo::get_blob(oid blob_id) const
{
	check_repo();
	git_blob* b = nullptr;
	exception::throw_on_error(git_blob_lookup(&b, repo_, &blob_id));
	return blob(b);
}

// --- revwalk --------------------------------------------------------------

revwalk repo::new_revwalk()
{
	check_repo();
	return revwalk(repo_);
}

// --- branches -------------------------------------------------------------

branch repo::lookup_branch(const std::string& name, git_branch_t type)
{
	check_repo();
	git_reference* out = nullptr;
	int err = git_branch_lookup(&out, repo_, name.c_str(), type);
	if (err < 0)
		exception::throw_on_error(err);
	return branch(out);
}

branch_iterator repo::new_branch_iterator(git_branch_t type)
{
	check_repo();
	return branch_iterator(repo_, type);
}

// --- tags -----------------------------------------------------------------

tag repo::lookup_tag(oid tag_id)
{
	check_repo();
	git_tag* t = nullptr;
	exception::throw_on_error(git_tag_lookup(&t, repo_, &tag_id));
	return tag(t);
}

// --- diff -----------------------------------------------------------------

diff repo::diff_trees(const tree& old_tree, const tree& new_tree)
{
	check_repo();
	git_diff* d = nullptr;
	exception::throw_on_error(git_diff_tree_to_tree(
		&d, repo_,
		const_cast<git_tree*>(old_tree.native_handle()),
		const_cast<git_tree*>(new_tree.native_handle()),
		nullptr));
	return diff(d);
}

diff repo::diff_tree_to_workdir(const tree& old_tree)
{
	check_repo();
	git_diff* d = nullptr;
	exception::throw_on_error(git_diff_tree_to_workdir(
		&d, repo_,
		const_cast<git_tree*>(old_tree.native_handle()),
		nullptr));
	return diff(d);
}

diff repo::diff_index_to_workdir()
{
	check_repo();
	git_diff* d = nullptr;
	exception::throw_on_error(git_diff_index_to_workdir(
		&d, repo_, nullptr, nullptr));
	return diff(d);
}

// ==========================================================================
//  Free functions
// ==========================================================================

bool is_git_repo(std::filesystem::path dir)
{
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

bool init_git_repo(std::filesystem::path repo_path,
                   const std::string& url /*= {}*/,
                   bool bare /*= false*/)
{
	git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
	opts.flags = GIT_REPOSITORY_INIT_MKPATH;
	if (bare)
		opts.flags |= GIT_REPOSITORY_INIT_BARE;
	opts.origin_url = url.empty() ? nullptr : url.c_str();
	opts.initial_head = "master";

	git_repository* repo = nullptr;
	int ret = git_repository_init_ext(&repo, repo_path.string().c_str(), &opts);
	if (repo)
		git_repository_free(repo);
	return ret == 0;
}

} // namespace gitpp
