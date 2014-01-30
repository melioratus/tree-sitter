#include "c_code.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>

using std::string;
using std::to_string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

namespace tree_sitter {
    namespace generate_code {
        static void str_replace(string &input, const string &search, const string &replace) {
            size_t pos = 0;
            while (1) {
                pos = input.find(search, pos);
                if (pos == string::npos) break;
                input.erase(pos, search.length());
                input.insert(pos, replace);
                pos += replace.length();
            }
        }
        
        string join(vector<string> lines, string separator) {
            string result;
            bool started = false;
            for (auto line : lines) {
                if (started) result += separator;
                started = true;
                result += line;
            }
            return result;
        }
        
        string join(vector<string> lines) {
            return join(lines, "\n");
        }
        
        string indent(string input) {
            string tab = "    ";
            str_replace(input, "\n", "\n" + tab);
            return tab + input;
        }
        
        string _switch(string condition, string body) {
            return join({
                "switch (" + condition + ") {",
                indent(body),
                "}"
            });
        }
        
        string _case(string value, string body) {
            return join({
                "case " + value + ":",
                indent(body), ""
            });
        }

        string _default(string body) {
            return join({
                "default:",
                indent(body)
            });
        }
        
        string _if(string condition, string body) {
            return join({
                "if (" + condition + ")",
                indent(body), ""
            });
        }
        
        class CCodeGenerator {
            const ParseTable parse_table;
            const LexTable lex_table;
            const string name;
        public:
            CCodeGenerator(string name, const ParseTable &parse_table, const LexTable &lex_table) :
                name(name),
                parse_table(parse_table),
                lex_table(lex_table)
                {}
            
            string symbol_id(rules::Symbol symbol) {
                if (symbol.is_auxiliary)
                    return "ts_aux_" + symbol.name;
                else
                    return "ts_symbol_" + symbol.name;
            }
            
            string character_code(char character) {
                switch (character) {
                    case '\0':
                        return "\\0";
                    case '"':
                        return "\\\"";
                    default:
                        return string() + character;
                }
            }
            
            string condition_for_character_match(const rules::CharacterMatch &match) {
                auto value = "LOOKAHEAD_CHAR()";
                switch (match.type) {
                    case rules::CharacterMatchTypeClass:
                        switch (match.value.character_class) {
                            case rules::CharClassDigit:
                                return string("isdigit(") + value + ")";
                            case rules::CharClassWord:
                                return string("isalnum(") + value + ")";
                        }
                    case rules::CharacterMatchTypeSpecific:
                        return string(value) + " == '" + character_code(match.value.character) + "'";
                    default:
                        return "";
                }
            }
            
            string condition_for_character_rule(const rules::Character &rule) {
                vector<string> parts;
                for (auto &match : rule.matches) {
                    parts.push_back(condition_for_character_match(match));
                }
                string result = join(parts, " || ");
                if (!rule.sign) result = "!(" + result + ")";
                return result;
            }
            
            string collapse_flags(vector<bool> flags) {
                string result;
                bool started = false;
                for (auto flag : flags) {
                    if (started) result += ", ";
                    result += (flag ? "1" : "0");
                    started = true;
                }
                return result;
            }
            
            string code_for_parse_actions(const unordered_set<ParseAction> &actions, const unordered_set<rules::Symbol> &expected_inputs) {
                auto action = actions.begin();
                if (action == actions.end()) {
                    return parse_error_call(expected_inputs);
                } else {
                    switch (action->type) {
                        case ParseActionTypeAccept:
                            return "ACCEPT_INPUT();";
                        case ParseActionTypeShift:
                            return "SHIFT(" + to_string(action->state_index) + ");";
                        case ParseActionTypeReduce:
                            return "REDUCE(" + symbol_id(action->symbol) + ", " + to_string(action->child_flags.size()) + ", COLLAPSE({" + collapse_flags(action->child_flags) + "}));";
                        default:
                            return "";
                    }
                }
            }
            
            string parse_error_call(const unordered_set<rules::Symbol> &expected_inputs) {
                string result = "PARSE_ERROR(" + to_string(expected_inputs.size()) + ", EXPECT({";
                bool started = false;
                for (auto symbol : expected_inputs) {
                    if (started) result += ", ";
                    started = true;
                    result += "\"" + symbol.name + "\"";
                }
                result += "}));";
                return result;
            }
            
            string escape_string(string input) {
                str_replace(input, "\"", "\\\"");
                return input;
            }
            
            string lex_error_call(const unordered_set<rules::Character> &expected_inputs) {
                unordered_set<rules::CharacterMatch> expected_matches;
                for (auto &rule : expected_inputs)
                    for (auto &match : rule.matches)
                        expected_matches.insert(match);
                
                string result = "LEX_ERROR(" + to_string(expected_matches.size()) + ", EXPECT({";
                bool started = false;
                for (auto match : expected_matches) {
                    if (started) result += ", ";
                    started = true;
                    result += "\"" + escape_string(match.to_string()) + "\"";
                }
                result += "}));";
                return result;
            }

            string code_for_lex_actions(const unordered_set<LexAction> &actions, const unordered_set<rules::Character> &expected_inputs) {
                auto action = actions.begin();
                if (action == actions.end()) {
                    return lex_error_call(expected_inputs);
                } else {
                    switch (action->type) {
                        case LexActionTypeAdvance:
                            return "ADVANCE(" + to_string(action->state_index) + ");";
                        case LexActionTypeAccept:
                            return "ACCEPT_TOKEN(" + symbol_id(action->symbol) + ");";
                        case LexActionTypeError:
                            return "";
                    }
                }
            }

            string code_for_parse_state(const ParseState &parse_state) {
                string body = "";
                for (auto pair : parse_state.actions)
                    body += _case(symbol_id(pair.first), code_for_parse_actions(pair.second, parse_state.expected_inputs()));
                body += _default(parse_error_call(parse_state.expected_inputs()));
                return
                    string("SET_LEX_STATE(") + to_string(parse_state.lex_state_index) + ");\n" +
                    _switch("LOOKAHEAD_SYM()", body);
            }

            string switch_on_lookahead_char(const LexState &parse_state) {
                string result = "";
                auto expected_inputs = parse_state.expected_inputs();
                for (auto pair : parse_state.actions)
                    result += _if(condition_for_character_rule(pair.first), code_for_lex_actions(pair.second, expected_inputs));
                result += code_for_lex_actions(parse_state.default_actions, expected_inputs);
                return result;
            }

            string switch_on_parse_state() {
                string body = "";
                for (int i = 0; i < parse_table.states.size(); i++)
                    body += _case(std::to_string(i), code_for_parse_state(parse_table.states[i]));
                body += _default("PARSE_PANIC();");
                return _switch("PARSE_STATE()", body);
            }

            string switch_on_lex_state() {
                string body = "";
                for (int i = 0; i < lex_table.states.size(); i++)
                    body += _case(std::to_string(i), switch_on_lookahead_char(lex_table.states[i]));
                body += _default("LEX_PANIC();");
                return _switch("LEX_STATE()", body);
            }
            
            string symbol_enum() {
                string result = "enum ts_symbol {\n";
                for (auto symbol : parse_table.symbols)
                    result += indent(symbol_id(symbol)) + ",\n";
                return result + "};";
            }

            string rule_names_list() {
                string result = "static const char *ts_symbol_names[] = {\n";
                for (auto symbol : parse_table.symbols)
                    result += indent(string("\"") + symbol.name) + "\",\n";
                return result + "};";
            }

            string includes() {
                return join({
                    "#include \"parser.h\"",
                    "#include <ctype.h>"
                });
            }
            
            string lex_function() {
                return join({
                    "static void ts_lex(TSParser *parser) {",
                    indent("START_LEXER();"),
                    indent(switch_on_lex_state()),
                    indent("FINISH_LEXER();"),
                    "}"
                });
            }
            
            string parse_function() {
                return join({
                    "static TSParseResult ts_parse(const char *input) {",
                    indent("START_PARSER();"),
                    indent(switch_on_parse_state()),
                    indent("FINISH_PARSER();"),
                    "}"
                });
            }
            
            string parse_config_struct() {
                return join({
                    "TSParseConfig ts_parse_config_" + name + " = {",
                    indent(".parse_fn = ts_parse,"),
                    indent(".symbol_names = ts_symbol_names"),
                    "};"
                });
            }
            
            string code() {
                return join({
                    includes(),
                    symbol_enum(),
                    rule_names_list(),
                    lex_function(),
                    parse_function(),
                    parse_config_struct(),
                }, "\n\n") + "\n";
            }
        };
        
        string c_code(string name, const ParseTable &parse_table, const LexTable &lex_table) {
            return CCodeGenerator(name, parse_table, lex_table).code();
        }
    }
}