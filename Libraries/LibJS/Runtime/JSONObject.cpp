/*
 * Copyright (c) 2020, Matthew Olsson <mattco@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Function.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <AK/JsonParser.h>
#include <AK/StringBuilder.h>
#include <AK/TypeCasts.h>
#include <AK/Utf16View.h>
#include <AK/Utf8View.h>
#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Lexer.h>
#include <LibJS/Parser.h>
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/BigIntObject.h>
#include <LibJS/Runtime/BooleanObject.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/JSONObject.h>
#include <LibJS/Runtime/NumberObject.h>
#include <LibJS/Runtime/Object.h>
#include <LibJS/Runtime/RawJSONObject.h>
#include <LibJS/Runtime/StringObject.h>
#include <LibJS/Runtime/ValueInlines.h>
#include <LibJS/Script.h>

namespace JS {

GC_DEFINE_ALLOCATOR(JSONObject);

JSONObject::JSONObject(Realm& realm)
    : Object(ConstructWithPrototypeTag::Tag, realm.intrinsics().object_prototype())
{
}

void JSONObject::initialize(Realm& realm)
{
    auto& vm = this->vm();
    Base::initialize(realm);
    u8 attr = Attribute::Writable | Attribute::Configurable;
    define_native_function(realm, vm.names.stringify, stringify, 3, attr);
    define_native_function(realm, vm.names.parse, parse, 2, attr);
    define_native_function(realm, vm.names.rawJSON, raw_json, 1, attr);
    define_native_function(realm, vm.names.isRawJSON, is_raw_json, 1, attr);

    // 25.5.3 JSON [ @@toStringTag ], https://tc39.es/ecma262/#sec-json-@@tostringtag
    define_direct_property(vm.well_known_symbol_to_string_tag(), PrimitiveString::create(vm, "JSON"_string), Attribute::Configurable);
}

// 25.5.2 JSON.stringify ( value [ , replacer [ , space ] ] ), https://tc39.es/ecma262/#sec-json.stringify
ThrowCompletionOr<Optional<String>> JSONObject::stringify_impl(VM& vm, Value value, Value replacer, Value space)
{
    auto& realm = *vm.current_realm();

    StringifyState state;

    if (replacer.is_object()) {
        if (replacer.as_object().is_function()) {
            state.replacer_function = &replacer.as_function();
        } else {
            auto is_array = TRY(replacer.is_array(vm));
            if (is_array) {
                auto& replacer_object = replacer.as_object();
                auto replacer_length = TRY(length_of_array_like(vm, replacer_object));
                Vector<Utf16String> list;
                for (size_t i = 0; i < replacer_length; ++i) {
                    auto replacer_value = TRY(replacer_object.get(i));
                    Optional<Utf16String> item;
                    if (replacer_value.is_string()) {
                        item = replacer_value.as_string().utf16_string();
                    } else if (replacer_value.is_number()) {
                        item = MUST(replacer_value.to_utf16_string(vm));
                    } else if (replacer_value.is_object()) {
                        auto& value_object = replacer_value.as_object();
                        if (is<StringObject>(value_object) || is<NumberObject>(value_object))
                            item = TRY(replacer_value.to_utf16_string(vm));
                    }
                    if (item.has_value() && !list.contains_slow(*item)) {
                        list.append(*item);
                    }
                }
                state.property_list = move(list);
            }
        }
    }

    if (space.is_object()) {
        auto& space_object = space.as_object();
        if (is<NumberObject>(space_object))
            space = TRY(space.to_number(vm));
        else if (is<StringObject>(space_object))
            space = TRY(space.to_primitive_string(vm));
    }

    if (space.is_number()) {
        auto space_mv = MUST(space.to_integer_or_infinity(vm));
        space_mv = min(10, space_mv);
        state.gap = space_mv < 1 ? String {} : MUST(String::repeated(' ', space_mv));
    } else if (space.is_string()) {
        auto string = space.as_string().utf8_string();
        if (string.bytes().size() <= 10)
            state.gap = string;
        else
            state.gap = MUST(string.substring_from_byte_offset(0, 10));
    } else {
        state.gap = String {};
    }

    auto wrapper = Object::create(realm, realm.intrinsics().object_prototype());
    MUST(wrapper->create_data_property_or_throw(Utf16String {}, value));
    return serialize_json_property(vm, state, Utf16String {}, wrapper);
}

// 25.5.2 JSON.stringify ( value [ , replacer [ , space ] ] ), https://tc39.es/ecma262/#sec-json.stringify
JS_DEFINE_NATIVE_FUNCTION(JSONObject::stringify)
{
    if (!vm.argument_count())
        return js_undefined();

    auto value = vm.argument(0);
    auto replacer = vm.argument(1);
    auto space = vm.argument(2);

    auto maybe_string = TRY(stringify_impl(vm, value, replacer, space));
    if (!maybe_string.has_value())
        return js_undefined();

    return PrimitiveString::create(vm, maybe_string.release_value());
}

// 25.5.2.1 SerializeJSONProperty ( state, key, holder ), https://tc39.es/ecma262/#sec-serializejsonproperty
// 1.4.1 SerializeJSONProperty ( state, key, holder ), https://tc39.es/proposal-json-parse-with-source/#sec-serializejsonproperty
ThrowCompletionOr<Optional<String>> JSONObject::serialize_json_property(VM& vm, StringifyState& state, PropertyKey const& key, Object* holder)
{
    // 1. Let value be ? Get(holder, key).
    auto value = TRY(holder->get(key));

    // 2. If Type(value) is Object or BigInt, then
    if (value.is_object() || value.is_bigint()) {
        // a. Let toJSON be ? GetV(value, "toJSON").
        auto to_json = TRY(value.get(vm, vm.names.toJSON));

        // b. If IsCallable(toJSON) is true, then
        if (to_json.is_function()) {
            // i. Set value to ? Call(toJSON, value, « key »).
            value = TRY(call(vm, to_json.as_function(), value, PrimitiveString::create(vm, key.to_string())));
        }
    }

    // 3. If state.[[ReplacerFunction]] is not undefined, then
    if (state.replacer_function) {
        // a. Set value to ? Call(state.[[ReplacerFunction]], holder, « key, value »).
        value = TRY(call(vm, *state.replacer_function, holder, PrimitiveString::create(vm, key.to_string()), value));
    }

    // 4. If Type(value) is Object, then
    if (value.is_object()) {
        auto& value_object = value.as_object();

        // a. If value has an [[IsRawJSON]] internal slot, then
        if (is<RawJSONObject>(value_object)) {
            // i. Return ! Get(value, "rawJSON").
            return MUST(value_object.get(vm.names.rawJSON)).as_string().utf8_string();
        }
        // b. If value has a [[NumberData]] internal slot, then
        if (is<NumberObject>(value_object)) {
            // i. Set value to ? ToNumber(value).
            value = TRY(value.to_number(vm));
        }
        // c. Else if value has a [[StringData]] internal slot, then
        else if (is<StringObject>(value_object)) {
            // i. Set value to ? ToString(value).
            value = TRY(value.to_primitive_string(vm));
        }
        // d. Else if value has a [[BooleanData]] internal slot, then
        else if (is<BooleanObject>(value_object)) {
            // i. Set value to value.[[BooleanData]].
            value = Value(static_cast<BooleanObject&>(value_object).boolean());
        }
        // e. Else if value has a [[BigIntData]] internal slot, then
        else if (is<BigIntObject>(value_object)) {
            // i. Set value to value.[[BigIntData]].
            value = Value(&static_cast<BigIntObject&>(value_object).bigint());
        }
    }

    // 5. If value is null, return "null".
    if (value.is_null())
        return "null"_string;

    // 6. If value is true, return "true".
    // 7. If value is false, return "false".
    if (value.is_boolean())
        return value.as_bool() ? "true"_string : "false"_string;

    // 8. If Type(value) is String, return QuoteJSONString(value).
    if (value.is_string())
        return quote_json_string(value.as_string().utf16_string_view());

    // 9. If Type(value) is Number, then
    if (value.is_number()) {
        // a. If value is finite, return ! ToString(value).
        if (value.is_finite_number())
            return MUST(value.to_string(vm));

        // b. Return "null".
        return "null"_string;
    }

    // 10. If Type(value) is BigInt, throw a TypeError exception.
    if (value.is_bigint())
        return vm.throw_completion<TypeError>(ErrorType::JsonBigInt);

    // 11. If Type(value) is Object and IsCallable(value) is false, then
    if (value.is_object() && !value.is_function()) {
        // a. Let isArray be ? IsArray(value).
        auto is_array = TRY(value.is_array(vm));

        // b. If isArray is true, return ? SerializeJSONArray(state, value).
        if (is_array)
            return TRY(serialize_json_array(vm, state, value.as_object()));

        // c. Return ? SerializeJSONObject(state, value).
        return TRY(serialize_json_object(vm, state, value.as_object()));
    }

    // 12. Return undefined.
    return Optional<String> {};
}

// 25.5.2.4 SerializeJSONObject ( state, value ), https://tc39.es/ecma262/#sec-serializejsonobject
ThrowCompletionOr<String> JSONObject::serialize_json_object(VM& vm, StringifyState& state, Object& object)
{
    if (state.seen_objects.contains(&object))
        return vm.throw_completion<TypeError>(ErrorType::JsonCircular);

    state.seen_objects.set(&object);
    String previous_indent = state.indent;
    state.indent = MUST(String::formatted("{}{}", state.indent, state.gap));
    Vector<String> property_strings;

    auto process_property = [&](PropertyKey const& key) -> ThrowCompletionOr<void> {
        if (key.is_symbol())
            return {};
        auto serialized_property_string = TRY(serialize_json_property(vm, state, key, &object));
        if (serialized_property_string.has_value()) {
            property_strings.append(MUST(String::formatted(
                "{}:{}{}",
                quote_json_string(key.to_string()),
                state.gap.is_empty() ? "" : " ",
                serialized_property_string)));
        }
        return {};
    };

    if (state.property_list.has_value()) {
        auto property_list = state.property_list.value();
        for (auto& property : property_list)
            TRY(process_property(property));
    } else {
        auto property_list = TRY(object.enumerable_own_property_names(PropertyKind::Key));
        for (auto& property : property_list)
            TRY(process_property(property.as_string().utf16_string()));
    }
    StringBuilder builder;
    if (property_strings.is_empty()) {
        builder.append("{}"sv);
    } else {
        bool first = true;
        builder.append('{');
        if (state.gap.is_empty()) {
            for (auto& property_string : property_strings) {
                if (!first)
                    builder.append(',');
                first = false;
                builder.append(property_string);
            }
        } else {
            builder.append('\n');
            builder.append(state.indent);
            auto separator = MUST(String::formatted(",\n{}", state.indent));
            for (auto& property_string : property_strings) {
                if (!first)
                    builder.append(separator);
                first = false;
                builder.append(property_string);
            }
            builder.append('\n');
            builder.append(previous_indent);
        }
        builder.append('}');
    }

    state.seen_objects.remove(&object);
    state.indent = previous_indent;
    return builder.to_string_without_validation();
}

// 25.5.2.5 SerializeJSONArray ( state, value ), https://tc39.es/ecma262/#sec-serializejsonarray
ThrowCompletionOr<String> JSONObject::serialize_json_array(VM& vm, StringifyState& state, Object& object)
{
    if (state.seen_objects.contains(&object))
        return vm.throw_completion<TypeError>(ErrorType::JsonCircular);

    state.seen_objects.set(&object);
    String previous_indent = state.indent;
    state.indent = MUST(String::formatted("{}{}", state.indent, state.gap));
    Vector<String> property_strings;

    auto length = TRY(length_of_array_like(vm, object));

    // Optimization
    property_strings.ensure_capacity(length);

    for (size_t i = 0; i < length; ++i) {
        auto serialized_property_string = TRY(serialize_json_property(vm, state, i, &object));
        if (!serialized_property_string.has_value()) {
            property_strings.append("null"_string);
        } else {
            property_strings.append(serialized_property_string.release_value());
        }
    }

    StringBuilder builder;
    if (property_strings.is_empty()) {
        builder.append("[]"sv);
    } else {
        if (state.gap.is_empty()) {
            builder.append('[');
            bool first = true;
            for (auto& property_string : property_strings) {
                if (!first)
                    builder.append(',');
                first = false;
                builder.append(property_string);
            }
            builder.append(']');
        } else {
            builder.append("[\n"sv);
            builder.append(state.indent);
            auto separator = MUST(String::formatted(",\n{}", state.indent));
            bool first = true;
            for (auto& property_string : property_strings) {
                if (!first)
                    builder.append(separator);
                first = false;
                builder.append(property_string);
            }
            builder.append('\n');
            builder.append(previous_indent);
            builder.append(']');
        }
    }

    state.seen_objects.remove(&object);
    state.indent = previous_indent;
    return builder.to_string_without_validation();
}

// 25.5.2.2 QuoteJSONString ( value ), https://tc39.es/ecma262/#sec-quotejsonstring
String JSONObject::quote_json_string(Utf16View const& string)
{
    // 1. Let product be the String value consisting solely of the code unit 0x0022 (QUOTATION MARK).
    StringBuilder builder;
    builder.append('"');

    // 2. For each code point C of StringToCodePoints(value), do
    for (auto code_point : string) {
        // a. If C is listed in the “Code Point” column of Table 70, then
        // i. Set product to the string-concatenation of product and the escape sequence for C as specified in the “Escape Sequence” column of the corresponding row.
        switch (code_point) {
        case '\b':
            builder.append("\\b"sv);
            break;
        case '\t':
            builder.append("\\t"sv);
            break;
        case '\n':
            builder.append("\\n"sv);
            break;
        case '\f':
            builder.append("\\f"sv);
            break;
        case '\r':
            builder.append("\\r"sv);
            break;
        case '"':
            builder.append("\\\""sv);
            break;
        case '\\':
            builder.append("\\\\"sv);
            break;
        default:
            // b. Else if C has a numeric value less than 0x0020 (SPACE), or if C has the same numeric value as a leading surrogate or trailing surrogate, then
            if (code_point < 0x20 || is_unicode_surrogate(code_point)) {
                // i. Let unit be the code unit whose numeric value is that of C.
                // ii. Set product to the string-concatenation of product and UnicodeEscape(unit).
                builder.appendff("\\u{:04x}", code_point);
            }
            // c. Else,
            else {
                // i. Set product to the string-concatenation of product and UTF16EncodeCodePoint(C).
                builder.append_code_point(code_point);
            }
        }
    }

    // 3. Set product to the string-concatenation of product and the code unit 0x0022 (QUOTATION MARK).
    builder.append('"');

    // 4. Return product.
    return builder.to_string_without_validation();
}

// 25.5.1 JSON.parse ( text [ , reviver ] ), https://tc39.es/ecma262/#sec-json.parse
// 1.2 JSON.parse ( text [ , reviver ] ), https://tc39.es/proposal-json-parse-with-source/#sec-json.parse
JS_DEFINE_NATIVE_FUNCTION(JSONObject::parse)
{
    auto& realm = *vm.current_realm();

    auto text = vm.argument(0);
    auto reviver = vm.argument(1);

    // 1. Let jsonString be ? ToString(text).
    auto json_string = TRY(text.to_string(vm));

    // 2. Parse StringToCodePoints(jsonString) as a JSON text as specified in ECMA-404.
    //    Throw a SyntaxError exception if it is not a valid JSON text as defined in that specification.
    auto json = JsonValue::from_string(json_string);
    if (json.is_error())
        return vm.throw_completion<SyntaxError>(ErrorType::JsonMalformed);

    // 3. Let scriptString be the string-concatenation of "(", jsonString, and ");".
    auto script_string = MUST(String::formatted("({});", json_string));

    // 4. Let script be ParseText(StringToCodePoints(scriptString), Script).
    auto parser = Parser { Lexer { script_string }, Program::Type::Script };
    parser.set_is_parsing_for_json_parse();
    auto program = parser.parse_program();

    // 5. NOTE: The early error rules defined in 13.2.5.1 have special handling for the above invocation of ParseText.

    // 6. Assert: script is a Parse Node.
    if (parser.has_errors())
        return vm.throw_completion<SyntaxError>(ErrorType::JsonMalformed);
    VERIFY(!parser.has_errors());

    // Create a Script record for evaluation
    auto script = realm.heap().allocate<Script>(realm, script_string, move(program));

    // Extract the JSON expression from the script for CreateJSONParseRecord
    RefPtr<Expression const> json_expression;
    if (script->parse_node().children().size() == 1) {
        if (auto* expr_stmt = dynamic_cast<ExpressionStatement const*>(script->parse_node().children()[0].ptr())) {
            json_expression = &expr_stmt->expression();
        }
    }
    if (!json_expression)
        return vm.throw_completion<SyntaxError>(ErrorType::JsonMalformed);

    // 7. Let completion be Completion(Evaluation of script).
    auto completion = vm.bytecode_interpreter().run(*script);

    // 8. NOTE: The PropertyDefinitionEvaluation semantics defined in 13.2.5.5 have special handling for the above evaluation.

    // 9. Let unfiltered be completion.[[Value]].
    auto unfiltered = TRY(completion);

    // 10. Assert: unfiltered is either a String, Number, Boolean, Null, or an Object that is defined by either an ArrayLiteral or an ObjectLiteral.
    VERIFY(unfiltered.is_string() || unfiltered.is_number() || unfiltered.is_boolean() || unfiltered.is_null() || unfiltered.is_object());

    // 11. If IsCallable(reviver) is true, then
    if (reviver.is_function()) {
        // a. Let root be OrdinaryObjectCreate(%Object.prototype%).
        auto root = Object::create(realm, realm.intrinsics().object_prototype());

        // b. Let rootName be the empty String.
        Utf16String root_name;

        // c. Perform ! CreateDataPropertyOrThrow(root, rootName, unfiltered).
        MUST(root->create_data_property_or_throw(root_name, unfiltered));

        // d. Let snapshot be CreateJSONParseRecord(script, rootName, unfiltered).
        auto json_string_utf16 = Utf16String::from_utf8(json_string);
        auto snapshot = create_json_parse_record(vm, json_expression, root_name, unfiltered, Utf16View { json_string_utf16 });

        // e. Return ? InternalizeJSONProperty(root, rootName, reviver, snapshot).
        return internalize_json_property(vm, root, root_name, reviver.as_function(), snapshot, Utf16View { json_string_utf16 });
    }
    // 12. Else,
    else {
        // a. Return unfiltered.
        return unfiltered;
    }
}

// 25.5.1.1 ParseJSON ( text ), https://tc39.es/ecma262/#sec-ParseJSON
ThrowCompletionOr<Value> JSONObject::parse_json(VM& vm, StringView text)
{
    auto json = JsonValue::from_string(text);

    // 1. If StringToCodePoints(text) is not a valid JSON text as specified in ECMA-404, throw a SyntaxError exception.
    if (json.is_error())
        return vm.throw_completion<SyntaxError>(ErrorType::JsonMalformed);

    // 2. Let scriptString be the string-concatenation of "(", text, and ");".
    // 3. Let script be ParseText(scriptString, Script).
    // 4. NOTE: The early error rules defined in 13.2.5.1 have special handling for the above invocation of ParseText.
    // 5. Assert: script is a Parse Node.
    // 6. Let result be ! Evaluation of script.
    auto result = JSONObject::parse_json_value(vm, json.value());

    // 7. NOTE: The PropertyDefinitionEvaluation semantics defined in 13.2.5.5 have special handling for the above evaluation.
    // 8. Assert: result is either a String, a Number, a Boolean, an Object that is defined by either an ArrayLiteral or an ObjectLiteral, or null.

    // 9. Return result.
    return result;
}

Value JSONObject::parse_json_value(VM& vm, JsonValue const& value)
{
    if (value.is_object())
        return Value(parse_json_object(vm, value.as_object()));
    if (value.is_array())
        return Value(parse_json_array(vm, value.as_array()));
    if (value.is_null())
        return js_null();
    if (auto double_value = value.get_double_with_precision_loss(); double_value.has_value())
        return Value(double_value.value());
    if (value.is_string())
        return PrimitiveString::create(vm, value.as_string());
    if (value.is_bool())
        return Value(value.as_bool());
    VERIFY_NOT_REACHED();
}

Object* JSONObject::parse_json_object(VM& vm, JsonObject const& json_object)
{
    auto& realm = *vm.current_realm();
    auto object = Object::create(realm, realm.intrinsics().object_prototype());
    json_object.for_each_member([&](auto& key, auto& value) {
        object->define_direct_property(Utf16String::from_utf8(key), parse_json_value(vm, value), default_attributes);
    });
    return object;
}

Array* JSONObject::parse_json_array(VM& vm, JsonArray const& json_array)
{
    auto& realm = *vm.current_realm();
    auto array = MUST(Array::create(realm, 0));
    size_t index = 0;
    json_array.for_each([&](auto& value) {
        array->define_direct_property(index++, parse_json_value(vm, value), default_attributes);
    });
    return array;
}

// 2 Static Semantics: ArrayLiteralContentNodes, https://tc39.es/proposal-json-parse-with-source/#sec-arrayliteralcontentnodes
static Vector<RefPtr<Expression const>> array_literal_content_nodes(ArrayExpression const& array_literal)
{
    Vector<RefPtr<Expression const>> elements;
    for (auto const& element : array_literal.elements()) {
        elements.append(element);
    }
    return elements;
}

// 3 Static Semantics: PropertyDefinitionNodes, https://tc39.es/proposal-json-parse-with-source/#sec-propertydefinitionnodes
static Vector<NonnullRefPtr<ObjectProperty const>> property_definition_nodes(ObjectExpression const& object_literal)
{
    Vector<NonnullRefPtr<ObjectProperty const>> properties;
    for (auto const& property : object_literal.properties()) {
        properties.append(property);
    }
    return properties;
}

// Helper to get PropName from a PropertyDefinition
static Optional<Utf16String> prop_name_of(ObjectProperty const& property)
{
    if (property.type() != ObjectProperty::Type::KeyValue)
        return {};

    if (auto* identifier = dynamic_cast<Identifier const*>(&property.key())) {
        return Utf16String { identifier->string() };
    } else if (auto* string_literal = dynamic_cast<StringLiteral const*>(&property.key())) {
        return string_literal->value();
    }

    return {};
}

// 1.2.4 Static Semantics: ShallowestContainedJSONValue, https://tc39.es/proposal-json-parse-with-source/#sec-shallowestcontainedjsonvalue
static RefPtr<Expression const> shallowest_contained_json_value(Expression const* node)
{
    // 1. Let F be the active function object.
    // 2. Assert: F is a JSON.parse built-in function object (see JSON.parse).

    // 3. Let types be « NullLiteral, BooleanLiteral, NumericLiteral, StringLiteral, ArrayLiteral, ObjectLiteral, UnaryExpression ».
    enum class JSONValueType {
        NullLiteral,
        BooleanLiteral,
        NumericLiteral,
        StringLiteral,
        ArrayLiteral,
        ObjectLiteral,
        UnaryExpression
    };

    Vector<JSONValueType> types = {
        JSONValueType::NullLiteral,
        JSONValueType::BooleanLiteral,
        JSONValueType::NumericLiteral,
        JSONValueType::StringLiteral,
        JSONValueType::ArrayLiteral,
        JSONValueType::ObjectLiteral,
        JSONValueType::UnaryExpression
    };

    // 4. Let unaryExpression be empty.
    RefPtr<Expression const> unary_expression;

    // 5. Let queue be « this Parse Node ».
    Vector<Expression const*> queue;
    queue.append(node);

    // 6. Repeat, while queue is not empty,
    while (!queue.is_empty()) {
        // a. Let candidate be the first element of queue.
        auto const* candidate = queue.first();

        // b. Remove the first element from queue.
        queue.take_first();

        // c. Let queuedChildren be false.
        bool queued_children = false;

        // d. For each nonterminal type of types, do
        for (auto type : types) {
            // i. If candidate is an instance of type, then
            bool is_instance = false;

            switch (type) {
            case JSONValueType::NullLiteral:
                is_instance = is<NullLiteral>(candidate);
                break;
            case JSONValueType::BooleanLiteral:
                is_instance = is<BooleanLiteral>(candidate);
                break;
            case JSONValueType::NumericLiteral:
                is_instance = is<NumericLiteral>(candidate);
                break;
            case JSONValueType::StringLiteral:
                is_instance = is<StringLiteral>(candidate);
                break;
            case JSONValueType::ArrayLiteral:
                is_instance = is<ArrayExpression>(candidate);
                break;
            case JSONValueType::ObjectLiteral:
                is_instance = is<ObjectExpression>(candidate);
                break;
            case JSONValueType::UnaryExpression:
                is_instance = is<UnaryExpression>(candidate) && static_cast<UnaryExpression const*>(candidate)->op() == UnaryOp::Minus;
                break;
            }

            if (is_instance) {
                // 1. NOTE: In the JSON grammar, a number token may represent a negative value.
                //    In ECMAScript, negation is represented as a unary operation.

                // 2. If type is UnaryExpression, then
                if (type == JSONValueType::UnaryExpression) {
                    // a. Set unaryExpression to candidate.
                    unary_expression = candidate;
                }
                // 3. Else if type is NumericLiteral, then
                else if (type == JSONValueType::NumericLiteral) {
                    // FIXME: NumericLiteral can exist without a UnaryExpression, so this if check is needed, but it doesn't match the spec.
                    // a. Assert: unaryExpression Contains candidate is true.
                    if (unary_expression) {
                        auto const* unary = static_cast<UnaryExpression const*>(unary_expression.ptr());
                        VERIFY(&unary->lhs() == candidate);
                        // b. Return unaryExpression.
                        return unary_expression;
                    }
                    // b. Return unaryExpression (or just the literal if no unary).
                    return candidate;
                }
                // 4. Else,
                else {
                    // a. Return candidate.
                    return candidate;
                }
            }

            // ii. If queuedChildren is false and candidate is an instance of a nonterminal and candidate Contains type is true, then
            if (!queued_children) {
                // Check if candidate is a nonterminal (all Expression nodes are nonterminals).
                // Check if candidate could contain nodes of 'type' (we need to check if continuing search makes sense).

                // For each type, check if candidate could possibly contain that type.
                // We simplify by checking if candidate is a compound expression that could have children.
                bool could_contain_type = false;

                // UnaryExpression can contain literals
                if (auto* unary = dynamic_cast<UnaryExpression const*>(candidate)) {
                    could_contain_type = true;

                    if (could_contain_type) {
                        // 1. Let children be a List containing each child node of candidate, in order.
                        Vector<Expression const*> children;
                        children.append(&unary->lhs());

                        // 2. Set queue to the list-concatenation of queue and children.
                        for (auto const* child : children) {
                            queue.append(child);
                        }

                        // 3. Set queuedChildren to true.
                        queued_children = true;
                    }
                }
                // Note: We could add more expression types here, but for JSON.parse context,
                // only UnaryExpression is relevant as a wrapper around NumericLiteral.
                // ArrayExpression and ObjectExpression are terminal JSON values (they match and return immediately).
            }
        }
    }

    // 7. Return empty.
    return nullptr;
}

// Helper to extract source text from an AST node using the original JSON source
// The parse node's source range is relative to the wrapped script "(json);",
// so we need to adjust offsets by -1 to account for the leading "(" and use the original JSON string
static String get_source_text_from_node(Expression const* node, Utf16View const& original_json_source)
{
    if (!node)
        return String {};

    auto source_range = node->source_range();

    // The source range is relative to the wrapped script "(json);"
    // We need to adjust by -1 for the leading "(" to get the position in the original JSON
    auto start_offset = source_range.start.offset;
    auto end_offset = source_range.end.offset;

    // Adjust for the wrapping parenthesis
    if (start_offset > 0)
        start_offset--;
    if (end_offset > 0)
        end_offset--;

    if (start_offset >= original_json_source.length_in_code_units() || end_offset > original_json_source.length_in_code_units())
        return String {};

    auto length = end_offset - start_offset;

    if (length == 0)
        return String {};

    auto view = original_json_source.substring_view(start_offset, length);
    auto source_text = MUST(view.to_utf8());

    return MUST(source_text.trim(" \t\r\n"sv, TrimMode::Both));
}

// FIXEME: remove original_source argument and extract it from parse_node instead
// 1.2.2 CreateJSONParseRecord ( parseNode, key, val ), https://tc39.es/proposal-json-parse-with-source/#sec-createjsonparserecord
JSONObject::JSONParseRecord JSONObject::create_json_parse_record(VM& vm, RefPtr<Expression const> parse_node, PropertyKey const& key, Value const& val, Utf16View const& original_source)
{
    // 1. Let typedValNode be ShallowestContainedJSONValue of parseNode.
    auto typed_val_node = shallowest_contained_json_value(parse_node.ptr());

    // 2. Assert: typedValNode is not empty.
    VERIFY(typed_val_node);

    // 3. Let elements be a new empty List.
    Vector<JSONParseRecord> elements;

    // 4. Let entries be a new empty List.
    Vector<JSONParseRecord> entries;

    // 5. If val is an Object, then
    if (val.is_object()) {
        auto& val_object = val.as_object();

        // a. Let isArray be ! IsArray(val).
        auto is_array = MUST(val.is_array(vm));

        // b. If isArray is true, then
        if (is_array) {
            // i. Assert: typedValNode is an ArrayLiteral Parse Node.
            VERIFY(is<ArrayExpression>(typed_val_node.ptr()));
            auto const& array_literal = static_cast<ArrayExpression const&>(*typed_val_node);

            // ii. Let contentNodes be ArrayLiteralContentNodes of typedValNode.
            auto content_nodes = array_literal_content_nodes(array_literal);

            // iii. Let len be the number of elements in contentNodes.
            auto len = content_nodes.size();

            // iv. Let valLen be ! LengthOfArrayLike(val).
            auto val_len = MUST(length_of_array_like(vm, val_object));

            // v. Assert: valLen = len.
            VERIFY(val_len == len);

            // vi. Let I be 0.
            size_t i = 0;

            // vii. Repeat, while I < len,
            while (i < len) {
                // 1. Let propName be ! ToString(𝔽(I)).
                auto prop_name = PropertyKey { i };

                // 2. Let elementParseRecord be CreateJSONParseRecord(contentNodes[I], propName, ! Get(val, propName)).
                auto element_value = MUST(val_object.get(prop_name));
                auto element_parse_record = create_json_parse_record(vm, content_nodes[i], prop_name, element_value, original_source);

                // 3. Append elementParseRecord to elements.
                elements.append(move(element_parse_record));

                // 4. Set I to I + 1.
                i = i + 1;
            }
        }
        // c. Else,
        else {
            // i. Assert: typedValNode is an ObjectLiteral Parse Node.
            VERIFY(is<ObjectExpression>(typed_val_node.ptr()));
            auto const& object_literal = static_cast<ObjectExpression const&>(*typed_val_node);

            // ii. Let propertyNodes be PropertyDefinitionNodes of typedValNode.
            auto property_nodes = property_definition_nodes(object_literal);

            // iii. NOTE: Because val was produced from JSON text and has not been modified, all of its property keys are Strings and will be exhaustively enumerated in source text order.

            // iv. Let keys be ! EnumerableOwnProperties(val, key).
            auto keys = MUST(val_object.enumerable_own_property_names(Object::PropertyKind::Key));

            // v. For each String P of keys, do
            for (auto& key_value : keys) {
                auto P = key_value.as_string().utf16_string();

                // 1. NOTE: In the case of JSON text specifying multiple name/value pairs with the same name for a single object (such as {"a":"lost","a":"kept"}), the value for the corresponding property of the resulting ECMAScript object is specified by the last pair with that name.

                // 2. Let propertyDefinition be empty.
                RefPtr<ObjectProperty const> property_definition;

                // 3. For each Parse Node propertyNode of propertyNodes, do
                for (auto const& property_node : property_nodes) {
                    // a. Let propName be PropName of propertyNode.
                    auto prop_name = prop_name_of(*property_node);

                    // b. If SameValue(propName, P) is true, set propertyDefinition to propertyNode.
                    if (prop_name.has_value() && *prop_name == P) {
                        property_definition = property_node;
                    }
                }

                // 4. Assert: propertyDefinition is PropertyDefinition : PropertyName : AssignmentExpression .
                VERIFY(property_definition);
                VERIFY(property_definition->type() == ObjectProperty::Type::KeyValue);

                // 5. Let propertyValueNode be the AssignmentExpression of propertyDefinition.
                auto const& property_value_node = property_definition->value();

                // 6. Let entryParseRecord be CreateJSONParseRecord(propertyValueNode, P, ! Get(val, P)).
                auto property_value = MUST(val_object.get(P));
                auto entry_parse_record = create_json_parse_record(vm, &property_value_node, P, property_value, original_source);

                // 7. Append entryParseRecord to entries.
                entries.append(move(entry_parse_record));
            }
        }
    }
    // 6. Else,
    //     a. Assert: typedValNode is not an ArrayLiteral Parse Node and not an ObjectLiteral Parse Node.
    else {
        VERIFY(!is<ArrayExpression>(typed_val_node.ptr()) && !is<ObjectExpression>(typed_val_node.ptr()));
    }

    // 7. Return the JSON Parse Record { [[ParseNode]]: typedValNode, [[Key]]: key, [[Value]]: val, [[Elements]]: elements, [[Entries]]: entries }.
    return JSONParseRecord { typed_val_node, key, val, move(elements), move(entries) };
}

// FIXME: remove original_source argument and extract it from parse_record instead
// 1.2.3 InternalizeJSONProperty ( holder, name, reviver, parseRecord ), https://tc39.es/proposal-json-parse-with-source/#sec-internalizejsonproperty
// 25.5.1.1 InternalizeJSONProperty ( holder, name, reviver ), https://tc39.es/ecma262/#sec-internalizejsonproperty
ThrowCompletionOr<Value> JSONObject::internalize_json_property(VM& vm, Object* holder, PropertyKey const& name, FunctionObject& reviver, Optional<JSONParseRecord> parse_record, Utf16View const& original_source)
{
    auto& realm = *vm.current_realm();

    // 1. Let val be ? Get(holder, name).
    auto val = TRY(holder->get(name));

    // 2. Let context be OrdinaryObjectCreate(%Object.prototype%).
    auto context = Object::create(realm, realm.intrinsics().object_prototype());

    Vector<JSONParseRecord> element_records;
    Vector<JSONParseRecord> entry_records;

    // 3. If parseRecord is a JSON Parse Record and SameValue(parseRecord.[[Value]], val) is true, then
    if (parse_record.has_value() && same_value(parse_record->value, val)) {
        // a. If val is not an Object, then
        if (!val.is_object()) {
            // i. Let parseNode be parseRecord.[[ParseNode]].
            auto parse_node = parse_record->parse_node;

            // ii. Assert: parseNode is not an ArrayLiteral Parse Node and not an ObjectLiteral Parse Node.
            VERIFY(parse_node && !is<ArrayExpression>(parse_node.ptr()) && !is<ObjectExpression>(parse_node.ptr()));

            // iii. Let sourceText be the source text matched by parseNode.
            auto source_text = get_source_text_from_node(parse_node.ptr(), original_source);

            // iv. Perform ! CreateDataPropertyOrThrow(context, "source", CodePointsToString(sourceText)).
            MUST(context->create_data_property_or_throw(vm.names.source, PrimitiveString::create(vm, source_text)));
        }
        // b. Let elementRecords be parseRecord.[[Elements]].
        element_records = parse_record->elements;
        // c. Let entryRecords be parseRecord.[[Entries]].
        entry_records = parse_record->entries;
    }

    // 4. Else,
    //     a. Let elementRecords be a new empty List.
    //     b. Let entryRecords be a new empty List.

    // 5. If val is an Object, then
    if (val.is_object()) {
        // a. Let isArray be ? IsArray(val).
        auto is_array = TRY(val.is_array(vm));

        auto& val_object = val.as_object();

        // b. If isArray is true, then
        if (is_array) {
            // ii. Let len be ? LengthOfArrayLike(val).
            auto len = TRY(length_of_array_like(vm, val_object));
            // iii. Let I be 0.
            // iv. Repeat, while I < len,
            for (size_t i = 0; i < len; ++i) {
                // 1. Let prop be ! ToString(𝔽(I)).
                auto prop = PropertyKey { i };
                // 2. If I < elementRecordsLen, let elementRecord be elementRecords[I]. Otherwise, let elementRecord be empty.
                Optional<JSONParseRecord> element_record;
                if (i < element_records.size())
                    element_record = element_records[i];
                // 3. Let newElement be ? InternalizeJSONProperty(val, prop, reviver, elementRecord).
                auto new_element = TRY(internalize_json_property(vm, &val_object, prop, reviver, element_record, original_source));
                // 4. If newElement is undefined, then
                if (new_element.is_undefined()) {
                    // a. Perform ? val.[[Delete]](prop).
                    TRY(val_object.internal_delete(prop));
                }
                // 5. Else,
                else {
                    // a. Perform ? CreateDataProperty(val, prop, newElement).
                    TRY(val_object.create_data_property(prop, new_element));
                }
                // 6. Set I to I + 1.
            }
        }
        // c. Else,
        else {
            // i. Let keys be ? EnumerableOwnProperties(val, key).
            auto keys = TRY(val_object.enumerable_own_property_names(Object::PropertyKind::Key));
            // ii. For each String P of keys, do
            for (auto& property_key : keys) {
                auto p = property_key.as_string().utf16_string();
                // 1. Let entryRecord be the element of entryRecords whose [[Key]] field is P. If there is no such element, let entryRecord be empty.
                Optional<JSONParseRecord> entry_record;
                for (auto& record : entry_records) {
                    if (record.key == PropertyKey { p }) {
                        entry_record = record;
                        break;
                    }
                }
                // 2. Let newElement be ? InternalizeJSONProperty(val, P, reviver, entryRecord).
                auto new_element = TRY(internalize_json_property(vm, &val_object, p, reviver, entry_record, original_source));
                // 3. If newElement is undefined, then
                if (new_element.is_undefined()) {
                    // a. Perform ? val.[[Delete]](P).
                    TRY(val_object.internal_delete(p));
                }
                // 4. Else,
                else {
                    // a. Perform ? CreateDataProperty(val, P, newElement).
                    TRY(val_object.create_data_property(p, new_element));
                }
            }
        }
    }

    // 6. Return ? Call(reviver, holder, « name, val, context »).
    return TRY(call(vm, reviver, holder, PrimitiveString::create(vm, name.to_string()), val, context));
}

// 1.3 JSON.rawJSON ( text ), https://tc39.es/proposal-json-parse-with-source/#sec-json.rawjson
JS_DEFINE_NATIVE_FUNCTION(JSONObject::raw_json)
{
    auto& realm = *vm.current_realm();

    // 1. Let jsonString be ? ToString(text).
    auto json_string = TRY(vm.argument(0).to_string(vm));

    // 2. Throw a SyntaxError exception if jsonString is the empty String, or if either the first or last code unit of
    //    jsonString is any of 0x0009 (CHARACTER TABULATION), 0x000A (LINE FEED), 0x000D (CARRIAGE RETURN), or
    //    0x0020 (SPACE).
    auto bytes = json_string.bytes_as_string_view();
    if (bytes.is_empty())
        return vm.throw_completion<SyntaxError>(ErrorType::JsonMalformed);

    static constexpr AK::Array invalid_code_points { 0x09, 0x0A, 0x0D, 0x20 };
    auto first_char = bytes[0];
    auto last_char = bytes[bytes.length() - 1];

    if (invalid_code_points.contains_slow(first_char) || invalid_code_points.contains_slow(last_char))
        return vm.throw_completion<SyntaxError>(ErrorType::JsonMalformed);

    // 3. Parse StringToCodePoints(jsonString) as a JSON text as specified in ECMA-404. Throw a SyntaxError exception
    //    if it is not a valid JSON text as defined in that specification, or if its outermost value is an object or
    //    array as defined in that specification.
    auto json = JsonValue::from_string(json_string);
    if (json.is_error())
        return vm.throw_completion<SyntaxError>(ErrorType::JsonMalformed);

    if (json.value().is_object() || json.value().is_array())
        return vm.throw_completion<SyntaxError>(ErrorType::JsonRawJSONNonPrimitive);

    // 4. Let internalSlotsList be « [[IsRawJSON]] ».
    // 5. Let obj be OrdinaryObjectCreate(null, internalSlotsList).
    auto object = RawJSONObject::create(realm, nullptr);

    // 6. Perform ! CreateDataPropertyOrThrow(obj, "rawJSON", jsonString).
    MUST(object->create_data_property_or_throw(vm.names.rawJSON, PrimitiveString::create(vm, json_string)));

    // 7. Perform ! SetIntegrityLevel(obj, frozen).
    MUST(object->set_integrity_level(Object::IntegrityLevel::Frozen));

    // 8. Return obj.
    return object;
}

// 1.1 JSON.isRawJSON ( O ), https://tc39.es/proposal-json-parse-with-source/#sec-json.israwjson
JS_DEFINE_NATIVE_FUNCTION(JSONObject::is_raw_json)
{
    // 1. If Type(O) is Object and O has an [[IsRawJSON]] internal slot, return true.
    if (vm.argument(0).is_object() && is<RawJSONObject>(vm.argument(0).as_object()))
        return Value(true);

    // 2. Return false.
    return Value(false);
}

}
