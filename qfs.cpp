/*
Copyright 2018 Aaron Yeoh Cruz

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "qfs.hpp"

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))

#include <vector>
#include <set>
#include <memory>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <climits>
#include <exception>

#endif

#if defined (__APPLE__) && defined (__MACH__)

#include <mach-o/dyld.h>
#include <cstdlib>

#endif

namespace qfs
{
	//TODO: follow UNIX standard as closely as possible
	std::string get_directory(const std::string& path)
	{
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))

		std::string::size_type delimiter_position = path.rfind("/");
		if (delimiter_position != std::string::npos) return path.substr(0, ++delimiter_position);
		else return ".";

#endif
	}

	//TODO: follow UNIX standard as closely as possible
	std::string get_base(const std::string& path)
	{
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))

		std::string::size_type delimiter_position = path.rfind("/");
		if (delimiter_position != std::string::npos) return path.substr(++delimiter_position);
		else return path;

#endif
	}

	std::string get_real_directory(const std::string& path)
	{
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__)) //TODO: Handle stat errors

		typedef std::pair<dev_t, ino_t> file_id;

		struct stat sb;

		if (stat(path.c_str(), &sb) == -1) throw std::runtime_error("No such directory"); //TODO: handle errno edge cases

		file_id current_id(sb.st_dev, sb.st_ino);

		if (stat("/", &sb) == -1) throw std::runtime_error("Could not read from root"); //TODO: handle errno edge cases

		std::vector<std::string> path_components;
		file_id root_id(sb.st_dev, sb.st_ino);
		std::string up_path(path + "/..");

		while (current_id != root_id)
		{
			bool pushed = false;
			DIR* dir = opendir(up_path.c_str());

			if (dir)
			{
				dirent* entry;
				up_path += "/";
				std::string::size_type after_slash = up_path.size();

				while ((entry = readdir(dir)))
				{
					if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
					{
						up_path.replace(after_slash, std::string::npos, entry->d_name);
						if (!lstat(up_path.c_str(), &sb))
						{
							file_id child_id(sb.st_dev, sb.st_ino);
							if (child_id == current_id)
							{
								path_components.push_back(entry->d_name);
								pushed = true;
								break;
							}
						}
					}
				}

				if (pushed && !fstat(dirfd(dir), &sb))
				{
					current_id = file_id(sb.st_dev, sb.st_ino);
					up_path.replace(after_slash, std::string::npos, "..");
				}

				closedir(dir);
			}

			if (!pushed) break;
		}

		if (current_id == root_id)
		{
			std::string real_path = "/";

			for (auto i = path_components.rbegin(); i != path_components.rend(); ++i)
			{
				real_path += *i + "/";
			}

			return real_path;
		}

		throw std::runtime_error("Real path of directory not found.");

#endif
	}

	std::string get_real_file(const std::string& path)
	{
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))

		return get_real_directory(get_directory(path)) + get_base(path);

#endif
	}

	std::string get_current_working_directory()
	{
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))

		return get_real_directory(".");

#endif
	}

	std::string get_symlink_chain_target(std::string path)
	{
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))

		typedef std::pair<dev_t, ino_t> file_id;

		if (!path.size()) throw std::invalid_argument("Empty path.");

		struct stat sb;
		std::set<file_id> seen_links;

		while (true)
		{
			if (lstat(path.c_str(), &sb) == -1) throw std::runtime_error("Dangling or nonexistent symlink chain."); //TODO: handle errno edge cases

			file_id current_id(sb.st_dev, sb.st_ino);

			if (seen_links.find(current_id) != seen_links.end()) throw std::runtime_error("Recursive symlink chain.");

			seen_links.insert(current_id);

			if (S_ISLNK(sb.st_mode))
			{
				if (sb.st_size <= 0) throw std::runtime_error("Unable to resolve symlink: invalid path length");
		
				std::unique_ptr<char[]> buffer(new char[sb.st_size + 1]);

				ssize_t amount = readlink(path.c_str(), buffer.get(), sb.st_size + 1);
				if (!((amount > 0) && (amount <= sb.st_size))) throw std::runtime_error("Unable to resolve symlink: link modified during check"); //If > length, it was modified mid check

				buffer[amount] = 0;

				if (buffer[0] == '/')
				{
					path = buffer.get();
				}
				else 
				{
					path = get_directory(path) + buffer.get();
				}
				continue;
			}
			else
			{
				break;
			}
		}

		return get_real_file(path);

#endif
	}

	std::string get_real_path(const std::string& path, bool should_resolve_symlinks)
	{
#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))

		if (!path.size()) throw std::invalid_argument("The path specified is empty."); //invalid argument
		
		struct stat sb;
		if (lstat(path.c_str(), &sb) == -1)
		{
			switch errno
			{
				case EACCES: throw std::runtime_error("Permission denied."); //runtime error

				// Handle ELOOP ourselves as there may not be a loop after all.
				case ELOOP: break;

				// Handle ENAMETOOLONG ourselves conditionally as PATH_MAX is ignored.
				case ENAMETOOLONG: if (get_base(path).size() <= NAME_MAX) break;
				case ENOENT:
				case ENOTDIR: throw std::runtime_error("The path specified is nonexistent"); //invalid argument

				default: throw std::runtime_error("An error occured reading from the filesystem."); //runtime error
			}
		}

		return (S_ISDIR(sb.st_mode)? get_real_directory(path) : (should_resolve_symlinks? get_symlink_chain_target(path) : get_real_file(path)));

#endif
	}

	std::string get_executable_path()
	{
#if defined (__APPLE__) && defined (__MACH__)

	uint32_t path_size = 0;
	_NSGetExecutablePath(nullptr, &path_size);

	std::unique_ptr<char[]> path(new char[path_size]);
	_NSGetExecutablePath(path.get(), &path_size);

	return get_real_path(path.get());

#elif defined (__linux__)

	return get_symlink_chain_target("/proc/self/exe");

#endif
	}
}