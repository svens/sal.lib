#include <sal/program_options/yaml_reader.hpp>
#include <sal/program_options/common.test.hpp>
#include <sstream>


namespace {

namespace po = sal::program_options;


struct yaml_reader
  : public sal_test::with_value<bool>
{
  std::vector<std::pair<std::string, std::string>> parse (const std::string &content)
  {
    std::cout << "\n\n-- 8< --" << content << "-- 8< --\n\n" << std::endl;

    std::istringstream iss;
    iss.str(content);
    po::yaml_reader_t parser{iss};

    po::option_set_t options;
    std::string option, argument;
    std::vector<std::pair<std::string, std::string>> result;
    while (parser(options, &option, &argument))
    {
      result.emplace_back(option, argument);
      option.clear();
      argument.clear();
    }

    return result;
  }


  void check (const std::vector<std::pair<std::string, std::string>> &result,
    const std::vector<std::pair<std::string, std::string>> &expected)
  {
    EXPECT_EQ(expected, result);
  }
};


INSTANTIATE_TEST_CASE_P(program_options, yaml_reader, testing::Values(true));


// flat successful cases: https://goo.gl/8l3BYx


TEST_P(yaml_reader, comment) //{{{1
{
  check(parse(R"(
#
# to be ignored
#

)"),
  {
  });
}


TEST_P(yaml_reader, option_no_argument) //{{{1
{
  check(parse(R"(
option:

)"),
  {
    { "option", "" },
  });
}


TEST_P(yaml_reader, option_argument) //{{{1
{
  check(parse(R"(
option_argument: argument

)"),
  {
    { "option_argument", "argument" },
  });
}


TEST_P(yaml_reader, option_newline_argument) //{{{1
{
  check(parse(R"(
option_newline_argument:
  argument

)"),
  {
    { "option_newline_argument", "argument" },
  });
}


TEST_P(yaml_reader, option_argument_newline_argument) //{{{1
{
  check(parse(R"(
option_argument_newline_argument: argument_1
  argument_2

)"),
  {
    { "option_argument_newline_argument", "argument_1 argument_2" },
  });
}


TEST_P(yaml_reader, option_multiline_argument) //{{{1
{
  check(parse(R"(
option_multiline_argument:
  argument_line_1
  argument_line_2

)"),
  {
    { "option_multiline_argument", "argument_line_1 argument_line_2" },
  });
}


TEST_P(yaml_reader, option_multiline_literal_argument) //{{{1
{
  check(parse(R"(
option_multiline_literal_argument: |
  argument_line_1
  argument_line_2

)"),
  {
    { "option_multiline_literal_argument", "argument_line_1\nargument_line_2\n" },
  });
}


TEST_P(yaml_reader, option_multiline_literal_keep_argument) //{{{1
{
  check(parse(R"(
option_multiline_literal_keep_argument: |+
  argument_line_1
  argument_line_2

)"),
  {
    { "option_multiline_literal_keep_argument", "argument_line_1\nargument_line_2\n\n" },
  });
}


TEST_P(yaml_reader, option_multiline_literal_strip_argument) //{{{1
{
  check(parse(R"(
option_multiline_literal_strip_argument: |-
  argument_line_1
  argument_line_2

)"),
  {
    { "option_multiline_literal_strip_argument", "argument_line_1\nargument_line_2" },
  });
}


TEST_P(yaml_reader, option_multiline_folded_argument) //{{{1
{
  check(parse(R"(
option_multiline_folded_argument: >
  argument_line_1
  argument_line_2

)"),
  {
    { "option_multiline_folded_argument", "argument_line_1 argument_line_2\n" },
  });
}


TEST_P(yaml_reader, option_multiline_folded_keep_argument) //{{{1
{
  check(parse(R"(
option_multiline_folded_keep_argument: >+
  argument_line_1
  argument_line_2

)"),
  {
    { "option_multiline_folded_keep_argument", "argument_line_1 argument_line_2\n\n" },
  });
}


TEST_P(yaml_reader, option_multiline_folded_strip_argument) //{{{1
{
  check(parse(R"(
option_multiline_folded_strip_argument: >-
  argument_line_1
  argument_line_2

)"),
  {
    { "option_multiline_folded_strip_argument", "argument_line_1 argument_line_2" },
  });
}


TEST_P(yaml_reader, option_list_argument) //{{{1
{
  check(parse(R"(
option_list_argument:
  - argument_1
  - argument_2

)"),
  {
    { "option_list_argument", "argument_1" },
    { "option_list_argument", "argument_2" },
  });
}


TEST_P(yaml_reader, option_list_argument_no_indent) //{{{1
{
  check(parse(R"(
option_list_argument_no_indent:
- argument_1
- argument_2

)"),
  {
    { "option_list_argument_no_indent", "argument_1" },
    { "option_list_argument_no_indent", "argument_2" },
  });
}


TEST_P(yaml_reader, option_list_literal_argument) //{{{1
{
  check(parse(R"(
option_list_literal_argument: |
  - argument_1
  - argument_2

)"),
  {
    { "option_list_literal_argument", "- argument_1\n- argument_2\n" },
  });
}


TEST_P(yaml_reader, option_list_folded_argument) //{{{1
{
  check(parse(R"(
option_list_folded_argument: >
  - argument_1
  - argument_2

)"),
  {
    { "option_list_folded_argument", "- argument_1 - argument_2\n" },
  });
}


//}}}1


// parser error cases


TEST_P(yaml_reader, error_double_colon)
{
  EXPECT_THROW(parse(R"(option::)"), po::parser_error);
}


TEST_P(yaml_reader, error_fold_no_colon)
{
  EXPECT_THROW(parse(R"(option>)"), po::parser_error);
}



TEST_P(yaml_reader, devel) //{{{1
{
  check(parse(R"(
a:

  X


  Z

b: >+
  X
  Z

c: |+

  X


  Z

x: |
  # komment
  proov

ends: here
)"),
  {
    { "a", "X\n\nZ" },
    { "b", "X Z\n\n" },
    { "c", "\nX\n\n\nZ\n\n" },
    { "x", "# komment\nproov\n" },
    { "ends", "here" },
  });
}


// }}}1


} // namespace
