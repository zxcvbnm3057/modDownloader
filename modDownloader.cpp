#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "Rpcrt4.lib")

#include <iostream>
#include <git2.h>
#include <string>
#include <map>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

/*
	初始化
	检查库
		检查当前分支名
		检查本地分支更新状态
	列出可用远程分支
	询问用户操作
		切换变体
		卸载
		更新
	执行操作
*/

int check_local_status(git_repository* repo, const char* branch);
bool list_remote_branches(std::map<int, std::string>* variant_list);
void switch_branch(git_repository* repo, std::string branch_name);
void uninstall();

enum INSTALL_STATUS
{
	UNKNOWN = 0,
	INSTALLED = 1,
	OUTDATE = 2,
	OK = 3,
};

enum ACTION_CODE {
	SWITCH = 'a',
	UPDATE = 'b',
	UNINSTALL = 'c'
};

int main()
{
	git_libgit2_init();
	git_repository* repo = nullptr;
	int repo_open_error;

	repo_open_error = git_repository_open_ext(&repo, "./", GIT_REPOSITORY_OPEN_NO_SEARCH, NULL);

	if (repo_open_error) {
		git_repository_init_options init_options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
		init_options.origin_url = "https://gitee.com/feng-ying/w3.git";
		git_repository_init_ext(&repo, "./.git", &init_options);
	}

	const char* branch = NULL;
	int status = check_local_status(repo, branch);
	printf("可用变体：\n");
	std::map<int, std::string> variant_list = std::map<int, std::string>();
	bool remote_status = list_remote_branches(&variant_list);

	char action_code;
	printf("请选择操作：\n");
	std::cout << static_cast<char>(SWITCH) << ": 切换变体" << "\t";
	if (status == OUTDATE)
		std::cout << static_cast<char>(UPDATE) << ": 更新" << "\t";
	if (status != UNKNOWN)
		std::cout << static_cast<char>(UNINSTALL) << ": 卸载" << "\t";
	std::cout << "\n";
	std::cin >> &action_code;
	switch (action_code)
	{
	case SWITCH:
		printf("输入目标变体编号：\n");
		char variant;
		std::cin >> variant;
		if (variant >= '0' && variant <= '9')
			switch_branch(repo, variant_list.at(variant - '0' + 1));
		break;
	case UPDATE:
		switch_branch(repo, std::string(branch));
		break;
	case UNINSTALL:
		switch_branch(repo, "bare");
		break;
	default:
		break;
	}

	git_repository_free(repo);
	git_libgit2_shutdown();
	if (action_code == UNINSTALL)
		uninstall();
}

static int check_local_status(git_repository* repo, const char* branch)
{
	int head_read_error, upstream_read_error, common_base_read_error, commit_read_error;
	git_reference* head = NULL;
	INSTALL_STATUS status = UNKNOWN;

	head_read_error = git_repository_head(&head, repo);

	std::cout << "当前状态： ";

	if (head_read_error == GIT_EUNBORNBRANCH)
		std::cout << "未初始化" << std::endl;
	else if (head_read_error)
		std::cout << "未知状态" << std::endl;
	else {
		status = INSTALLED;
		branch = git_reference_shorthand(head);
		std::cout << branch;
		git_reference* local_branch = nullptr;
		git_branch_lookup(&local_branch, repo, branch, GIT_BRANCH_LOCAL);
		git_reference* upstream = nullptr;
		upstream_read_error = git_branch_upstream(&upstream, head);
		if (!upstream_read_error)
		{
			git_oid merge_base_oid;
			common_base_read_error = git_merge_base(&merge_base_oid, repo, git_reference_target(head), git_reference_target(upstream));
			if (!common_base_read_error)
			{
				git_commit* local_commit = nullptr;
				commit_read_error = git_commit_lookup(&local_commit, repo, git_reference_target(head));
				if (!commit_read_error)
					if (git_oid_cmp(&merge_base_oid, git_commit_id(local_commit)) != 0) {
						std::cout << "（已过时）" << std::endl;
						status = OUTDATE;
					}
					else {
						std::cout << "（已是最新版本）" << std::endl;
						status = OK;
					}
				git_commit_free(local_commit);
				goto exit;
			}
		}
		std::cout << "（检查更新失败）" << std::endl;
	exit:
		return status;
	}
}

static bool list_remote_branches(std::map<int, std::string>* variant_list)
{
	git_remote* remote = nullptr;
	int error = git_remote_create_detached(&remote, "https://gitee.com/feng-ying/w3.git");

	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;
	error = git_remote_connect(remote, GIT_DIRECTION_FETCH, &callbacks, NULL, NULL);

	const git_remote_head** remote_head = NULL;
	size_t remote_head_count = 0;
	git_remote_ls(&remote_head, &remote_head_count, remote);

	for (size_t i = 0, j = 0; i < remote_head_count; i++) {
		char* name = remote_head[i]->name;
		if (strncmp(name, "refs/heads/", 11) == 0) {
			printf("\t%zu: %s\n", j++, name + 11);
			variant_list->emplace(j, name + 11);
		}
	}
	git_remote_free(remote);
	return true;
}

static void switch_branch(git_repository* repo, std::string branch_name) {
	git_remote* remote = nullptr;
	git_remote_lookup(&remote, repo, "origin");
	std::string refspec_str = "refs/heads/" + branch_name;
	char* refspec = { const_cast<char*>(refspec_str.c_str()) };
	git_strarray upstream_array = {
		&refspec,
		1
	};
	git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
	fetch_opts.depth = 1;
	fetch_opts.prune = GIT_FETCH_PRUNE;
	git_remote_fetch(remote, &upstream_array, &fetch_opts, nullptr);

	git_reference* remote_ref = nullptr;
	git_reference_lookup(&remote_ref, repo, ("refs/remotes/origin/" + branch_name).c_str());

	git_object* target_commit = nullptr;
	git_reference_peel(&target_commit, remote_ref, GIT_OBJECT_COMMIT);

	git_reference* local_ref = nullptr;
	if (git_branch_create(&local_ref, repo, branch_name.c_str(), (git_commit*)target_commit, true))
		git_branch_lookup(&local_ref, repo, branch_name.c_str(), GIT_BRANCH_LOCAL);
	git_branch_set_upstream(local_ref, ("origin/" + branch_name).c_str());

	git_repository_set_head(repo, ("refs/heads/" + branch_name).c_str());

	git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
	checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
	git_checkout_head(repo, &checkout_opts);
	git_reset(repo, target_commit, GIT_RESET_HARD, &checkout_opts);

	git_reference_free(remote_ref);
	git_remote_free(remote);
	return;
}

static void uninstall() {
	fs::path git_dir = "./.git";
	fs::path gitignore = "./.gitignore";

	try {
		if (fs::exists(git_dir)) {
			fs::remove_all(git_dir);
		}
		if (fs::exists(gitignore)) {
			fs::remove_all(git_dir);
		}
		std::cout << "已完成卸载，但仍建议在游玩前使用steam检测一次游戏完整性。" << std::endl;
	}
	catch (const fs::filesystem_error& e) {
		std::cout << "卸载失败，请关闭其他相关应用后重试（例如巫师3游戏或本应用的其他实例）" << std::endl;
	}
}