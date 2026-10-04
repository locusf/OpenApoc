#pragma once
// Minimal header-only stand-in for the tiny slice of boost::program_options
// used by OpenApoc's offline code-generator tools
// (tools/code_generators/gamestate_serialize_gen/main.cpp and
// tools/code_generators/luagamestate_support_gen/main.cpp).
//
// Why this exists: those two generators are plain host-side command-line
// tools invoked at build time to produce
// gamestate_serialize_generated.{h,cpp} and
// luagamestate_support_generated.{h,cpp}. They must run on the *build
// machine's* architecture, but this project only ships ARM64 multiarch
// Boost development libraries for the target device -- there is no native
// Boost for the build host. Rather than skip or stub out the generators
// (which would silently break gamestate serialization/Lua bindings), this
// header reproduces the exact small surface area the two main.cpp files
// use, so the real generator logic (serialization_xml.cpp parsing +
// writeHeader/writeSource) is preserved unchanged, and only the
// command-line-option plumbing is reimplemented.
//
// This header is only ever added to the include path for the *host*
// build of the generators (see the build recipe); the target ARM64 build
// of the main OpenApoc application and its generators continues to use
// the genuine Boost (program_options/locale/date_time) multiarch
// libraries, unaffected by this file.
//
// Supported surface (matches exactly what the two generator mains use):
//   options_description::add_options()
//       ("long,short", "description")                       -- flag
//       ("long,short", po::value<std::string>(), "descr")    -- valued
//   variables_map, vm.count(name), vm[name].as<std::string>()
//   parse_command_line(argc, argv, desc), store(parsed, vm), notify(vm)
//   operator<<(ostream&, options_description) for usage/error messages

#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace boost
{
namespace program_options
{

template <typename T> class typed_value
{
  public:
	typed_value() = default;
};

template <typename T> inline typed_value<T> value() { return typed_value<T>(); }

struct option_spec
{
	std::string long_name;
	std::string short_name;
	std::string description;
	bool takes_value = false;
};

class options_description
{
  public:
	explicit options_description(std::string caption) : caption(std::move(caption)) {}

	class adder
	{
	  public:
		explicit adder(options_description &owner) : owner(owner) {}

		adder &operator()(const std::string &names, const std::string &description)
		{
			owner.add(names, description, false);
			return *this;
		}

		template <typename T>
		adder &operator()(const std::string &names, const typed_value<T> &,
		                   const std::string &description)
		{
			owner.add(names, description, true);
			return *this;
		}

	  private:
		options_description &owner;
	};

	adder add_options() { return adder(*this); }

	void add(const std::string &names, const std::string &description, bool takes_value)
	{
		option_spec spec;
		auto comma = names.find(',');
		if (comma == std::string::npos)
		{
			spec.long_name = names;
		}
		else
		{
			spec.long_name = names.substr(0, comma);
			spec.short_name = names.substr(comma + 1);
		}
		spec.description = description;
		spec.takes_value = takes_value;
		options.push_back(spec);
	}

	const std::vector<option_spec> &getOptions() const { return options; }

	friend std::ostream &operator<<(std::ostream &os, const options_description &desc)
	{
		os << desc.caption << ":\n";
		for (auto &opt : desc.options)
		{
			os << "  --" << opt.long_name;
			if (!opt.short_name.empty())
			{
				os << " [-" << opt.short_name << "]";
			}
			os << " : " << opt.description << "\n";
		}
		return os;
	}

  private:
	std::string caption;
	std::vector<option_spec> options;
};

class variable_value
{
  public:
	variable_value() = default;
	explicit variable_value(std::string value) : present(true), stored(std::move(value)) {}

	template <typename T> const T &as() const;

	bool has_value() const { return present; }

  private:
	bool present = false;
	std::string stored;
};

template <> inline const std::string &variable_value::as<std::string>() const { return stored; }

class variables_map
{
  public:
	std::size_t count(const std::string &name) const { return values.count(name) != 0 ? 1 : 0; }

	variable_value &operator[](const std::string &name) { return values[name]; }

	void set(const std::string &name, std::string value)
	{
		values[name] = variable_value(std::move(value));
	}

  private:
	std::map<std::string, variable_value> values;
};

struct parsed_options
{
	std::vector<std::pair<std::string, std::string>> entries; // (long_name, value-or-empty)
};

inline parsed_options parse_command_line(int argc, char **argv, const options_description &desc)
{
	parsed_options parsed;
	const auto &opts = desc.getOptions();

	auto findByLong = [&](const std::string &name) -> const option_spec * {
		for (auto &o : opts)
		{
			if (o.long_name == name)
				return &o;
		}
		return nullptr;
	};
	auto findByShort = [&](const std::string &name) -> const option_spec * {
		for (auto &o : opts)
		{
			if (o.short_name == name)
				return &o;
		}
		return nullptr;
	};

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		const option_spec *spec = nullptr;
		std::string name;
		if (arg.rfind("--", 0) == 0)
		{
			name = arg.substr(2);
			spec = findByLong(name);
		}
		else if (arg.rfind("-", 0) == 0)
		{
			name = arg.substr(1);
			spec = findByShort(name);
			if (spec)
			{
				name = spec->long_name;
			}
		}
		else
		{
			continue;
		}
		if (!spec)
		{
			continue;
		}
		if (spec->takes_value)
		{
			if (i + 1 < argc)
			{
				parsed.entries.emplace_back(name, std::string(argv[++i]));
			}
		}
		else
		{
			parsed.entries.emplace_back(name, std::string());
		}
	}
	return parsed;
}

inline void store(const parsed_options &parsed, variables_map &vm)
{
	for (auto &entry : parsed.entries)
	{
		vm.set(entry.first, entry.second);
	}
}

inline void notify(variables_map &) {}

} // namespace program_options
} // namespace boost
