test("reviver receives context parameter", () => {
    let contextReceived = false;
    JSON.parse("42", (key, value, context) => {
        contextReceived = context !== undefined;
        return value;
    });
    expect(contextReceived).toBeTrue();
});

test("context is plain object with Object.prototype", () => {
    JSON.parse('[1, "text", true, null]', (key, value, context) => {
        expect(typeof context).toBe("object");
        expect(Object.getPrototypeOf(context)).toBe(Object.prototype);
        return value;
    });
});

test("context has source property for primitive values", () => {
    const sources = [];
    JSON.parse('[1, "text", true, null]', (key, value, context) => {
        if (value !== undefined && typeof value !== "object") {
            sources.push(context.source);
        }
        return value;
    });
    expect(sources).toEqual(["1", '"text"', "true"]);
});

test("context has no source property for objects and arrays", () => {
    const contexts = [];
    JSON.parse('[1, [2], {"a": 3}]', (key, value, context) => {
        if (
            Array.isArray(value) ||
            (typeof value === "object" && value !== null && !Array.isArray(value))
        ) {
            contexts.push(Object.keys(context));
        }
        return value;
    });
    contexts.forEach(keys => {
        expect(keys.length).toBe(0);
    });
});

test("source text for various number formats", () => {
    const testCases = [
        ["1", "1"],
        ["1.1", "1.1"],
        ["-1", "-1"],
        ["-1.1", "-1.1"],
        ["1.1e1", "1.1e1"],
        ["1.1e+1", "1.1e+1"],
        ["1.1e-1", "1.1e-1"],
        ["1.1E1", "1.1E1"],
    ];

    testCases.forEach(([input, expectedSource]) => {
        JSON.parse(input, (key, value, context) => {
            if (key === "") {
                expect(context.source).toBe(expectedSource);
            }
            return value;
        });
    });
});

test("source text for literals", () => {
    const literals = [
        ["null", "null"],
        ["true", "true"],
        ["false", "false"],
        ['"foo"', '"foo"'],
    ];

    literals.forEach(([input, expectedSource]) => {
        JSON.parse(input, (key, value, context) => {
            if (key === "") {
                expect(context.source).toBe(expectedSource);
            }
            return value;
        });
    });
});

test("array with mixed types", () => {
    const expectedSources = ["1", '"2"', "true", "null", "1", "1"];
    let index = 0;

    JSON.parse('[1, "2", true, null, {"x": 1, "y": 1}]', (key, value, context) => {
        if (context.source !== undefined) {
            expect(context.source).toBe(expectedSources[index++]);
        }
        return value;
    });

    expect(index).toBe(6);
});

test("nested object", () => {
    const expectedSources = ["1", "2"];
    let index = 0;

    const result = JSON.parse('{"x": {"x": 1, "y": 2}}', (key, value, context) => {
        if (context.source !== undefined) {
            expect(context.source).toBe(expectedSources[index++]);
        }
        return value;
    });

    expect(index).toBe(2);
    expect(result).toEqual({ x: { x: 1, y: 2 } });
});

test("context.source property attributes", () => {
    JSON.parse("42", (key, value, context) => {
        if (context.source !== undefined) {
            const descriptor = Object.getOwnPropertyDescriptor(context, "source");
            expect(descriptor.configurable).toBeTrue();
            expect(descriptor.enumerable).toBeTrue();
            expect(descriptor.writable).toBeTrue();
        }
        return value;
    });
});

test("array forward modify with append", () => {
    const log = [];
    JSON.parse("[1,[]]", function (k, v, { source }) {
        log.push(`key: |${k}| value: ${JSON.stringify(v)} source: |${source}|`);
        if (v === 1) {
            this[1].push("barf");
        }
        return this[k];
    });

    expect(log).toEqual([
        "key: |0| value: 1 source: |1|",
        'key: |0| value: "barf" source: |undefined|',
        'key: |1| value: ["barf"] source: |undefined|',
        'key: || value: [1,["barf"]] source: |undefined|',
    ]);
});

test("object forward modify with added property", () => {
    const log = [];
    JSON.parse('{"p":1,"q":{}}', function (k, v, { source }) {
        log.push(`key: |${k}| value: ${JSON.stringify(v)} source: |${source}|`);
        if (v === 1) {
            this.q.added = "barf";
        }
        return this[k];
    });

    expect(log).toEqual([
        "key: |p| value: 1 source: |1|",
        'key: |added| value: "barf" source: |undefined|',
        'key: |q| value: {"added":"barf"} source: |undefined|',
        'key: || value: {"p":1,"q":{"added":"barf"}} source: |undefined|',
    ]);
});

test("forward modifications lose source text", () => {
    const replacements = [42, "foo", ["bar"], { baz: "qux" }];

    replacements.forEach(replacement => {
        let sawReplacementWithoutSource = false;
        JSON.parse("[1, 2]", function (k, v, { source }) {
            if (k === "0") {
                this[1] = replacement;
            } else if (k === "1" && v === replacement) {
                sawReplacementWithoutSource = source === undefined;
            }
            return this[k];
        });
        expect(sawReplacementWithoutSource).toBeTrue();
    });
});

test("reviver still gets 3 arguments even without modifications", () => {
    let argCounts = [];
    JSON.parse('{"a": 1}', function (k, v, context) {
        argCounts.push(arguments.length);
        return v;
    });
    argCounts.forEach(count => {
        expect(count).toBe(3);
    });
});

test("number source text: zero", () => {
    let source;
    JSON.parse("0", (k, v, { source: s }) => {
        source = s;
        return v;
    });
    expect(source).toBe("0");
});

test("number source text: negative integers", () => {
    const cases = ["-1", "-42"];
    cases.forEach(input => {
        let source;
        JSON.parse(input, (k, v, { source: s }) => {
            source = s;
            return v;
        });
        expect(source).toBe(input);
    });
});

test("number source text: decimals", () => {
    const cases = ["1.5", "-1.5", "0.0"];
    cases.forEach(input => {
        let source;
        JSON.parse(input, (k, v, { source: s }) => {
            source = s;
            return v;
        });
        expect(source).toBe(input);
    });
});

test("number source text: exponents", () => {
    const cases = ["1e2", "1e+2", "1e-2", "1.5e2", "-1.5e-2", "1E2", "1E+2", "1E-2"];
    cases.forEach(input => {
        let source;
        JSON.parse(input, (k, v, { source: s }) => {
            source = s;
            return v;
        });
        expect(source).toBe(input);
    });
});

test("string source text: various strings", () => {
    const cases = [
        ['""', '""'],
        ['"hello"', '"hello"'],
        ['"hello world"', '"hello world"'],
        ['"hello\\"world"', '"hello\\"world"'],
        ['"line1\\nline2"', '"line1\\nline2"'],
    ];
    cases.forEach(([input, expected]) => {
        let source;
        JSON.parse(input, (k, v, { source: s }) => {
            source = s;
            return v;
        });
        expect(source).toBe(expected);
    });
});

test("empty arrays have no source", () => {
    const sources = [];
    JSON.parse("[]", (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual([]);
});

test("arrays preserve element source text", () => {
    const sources = [];
    JSON.parse("[1,2,3]", (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", "2", "3"]);
});

test("arrays with negative numbers", () => {
    const sources = [];
    JSON.parse("[-1, -2]", (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["-1", "-2"]);
});

test("arrays with floats", () => {
    const sources = [];
    JSON.parse("[1.1, 2.2]", (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1.1", "2.2"]);
});

test("nested arrays", () => {
    const sources = [];
    JSON.parse("[[1, 2], [3, 4]]", (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", "2", "3", "4"]);
});

test("empty objects have no source", () => {
    const sources = [];
    JSON.parse("{}", (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual([]);
});

test("objects with multiple properties", () => {
    const sources = [];
    JSON.parse('{"a":1,"b":2}', (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", "2"]);
});

test("objects with mixed value types", () => {
    const sources = [];
    JSON.parse('{"a":1,"b":"text","c":true,"d":null}', (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", '"text"', "true", "null"]);
});

test("deeply nested objects", () => {
    const sources = [];
    JSON.parse('{"a":{"b":1,"c":2}}', (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", "2"]);
});

test("object containing array", () => {
    const sources = [];
    JSON.parse('{"a":[1,2]}', (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", "2"]);
});

test("array containing object", () => {
    const sources = [];
    JSON.parse('[{"a":1}]', (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1"]);
});

test("complex mixed nesting", () => {
    const sources = [];
    JSON.parse('{"a":[1,{"b":2}],"c":3}', (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", "2", "3"]);
});

test("whitespace in arrays is ignored", () => {
    const sources = [];
    JSON.parse("[ 1 , 2 ]", (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", "2"]);
});

test("whitespace in objects is ignored", () => {
    const sources = [];
    JSON.parse('{ "a" : 1 }', (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1"]);
});

test("newlines in arrays are ignored", () => {
    const sources = [];
    JSON.parse("[\n1,\n2\n]", (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1", "2"]);
});

test("newlines in objects are ignored", () => {
    const sources = [];
    JSON.parse('{\n"a":1\n}', (k, v, { source }) => {
        if (source !== undefined) sources.push(source);
        return v;
    });
    expect(sources).toEqual(["1"]);
});

test("negative zero source text", () => {
    let source;
    JSON.parse("-0", (k, v, { source: s }) => {
        source = s;
        return v;
    });
    expect(source).toBe("-0");
});

test("Different cases", () => {
    const json = {
        parserTestSuite: {
            version: 1.5,
            metadata: {
                testId: "COMPLEX_001",
                timestamp: "2025-10-04T16:09:00Z",
                active: true,
            },
            primitives: {
                nullValue: null,
                booleanTrue: true,
                booleanFalse: false,
                integerPositive: 1234567890,
                integerNegative: -987654321,
                floatStandard: 3.14159265,
                floatNegative: -0.0001,
                floatScientificPositive: 1.2e100,
                floatScientificNegative: -4.567e-50,
                floatZero: 0.0,
                floatPointZero: 5,
            },
            stringsAndEncoding: {
                stringSimple: "Hello World!",
                stringWithQuotes: "A string with \"double quotes\" and a 'single quote'.",
                stringWithEscapes:
                    "Line break:\\n Tab:\\t Backslash:\\\\ Forward slash:\\/ Return:\\r",
                stringUnicodeBMP: "Greek: \u03a3, Math: \u221a, Currency: \u20ac",
                stringUnicodeAstral: "Emoji: \ud83d\ude00 (Grinning Face)",
                stringEmpty: "",
            },
            arrays: {
                arrayEmpty: [],
                arraySimple: [1, 2, 3, 4, 5],
                arrayMixedTypes: ["string", 100, true, null, -9.99e-9],
                arrayNested: [
                    [1, 2],
                    [3, 4, [5]],
                    [
                        {
                            key: "value",
                        },
                    ],
                ],
            },
            objects: {
                objectEmpty: {},
                objectWithNumericKeys: {
                    "1key": "value1",
                    "2key": "value2",
                },
                objectWithSpecialCharKeys: {
                    "key-with-hyphen": true,
                    "key space": false,
                    "key.dot": 0,
                },
                deeplyNestedObject: {
                    level1: {
                        level2: {
                            level3: {
                                finalKey: [
                                    1,
                                    2,
                                    {
                                        test: "deep",
                                    },
                                ],
                            },
                        },
                    },
                },
                arrayContainingObjects: [
                    {
                        id: 1,
                        data: "A",
                    },
                    {
                        id: 2,
                        data: "B",
                    },
                    {
                        id: 3,
                        data: "C",
                    },
                ],
            },
            complexStructure: [
                {
                    type: "user",
                    name: "Jane Doe",
                    attributes: {
                        isAdmin: true,
                        projects: ["ProjectX", "ProjectY"],
                    },
                    history: null,
                },
                {
                    type: "transaction",
                    id: "T-98765",
                    amount: 1000000.55,
                    details: [
                        {
                            item: "Laptop",
                            qty: 1,
                            price: 1200.0,
                        },
                        {
                            item: "Software License",
                            qty: 10,
                            price: 5.5,
                        },
                    ],
                    status: "COMPLETED",
                },
            ],
        },
    };

    const expected = [
        "1.5",
        '"COMPLEX_001"',
        '"2025-10-04T16:09:00Z"',
        "true",
        null,
        "null",
        "true",
        "false",
        "1234567890",
        "-987654321",
        "3.14159265",
        "-0.0001",
        "1.2e+100",
        "-4.567e-50",
        "0",
        "5",
        null,
        '"Hello World!"',
        '"A string with \\"double quotes\\" and a \'single quote\'."',
        '"Line break:\\\\n Tab:\\\\t Backslash:\\\\\\\\ Forward slash:\\\\/ Return:\\\\r"',
        '"Greek: Σ, Math: √, Currency: €"',
        '"Emoji: 😀 (Grinning Face)"',
        '""',
        null,
        null,
        "1",
        "2",
        "3",
        "4",
        "5",
        null,
        '"string"',
        "100",
        "true",
        "null",
        "-9.99e-9",
        null,
        "1",
        "2",
        null,
        "3",
        "4",
        "5",
        null,
        null,
        '"value"',
        null,
        null,
        null,
        null,
        null,
        '"value1"',
        '"value2"',
        null,
        "true",
        "false",
        "0",
        null,
        "1",
        "2",
        '"deep"',
        null,
        null,
        null,
        null,
        null,
        null,
        "1",
        '"A"',
        null,
        "2",
        '"B"',
        null,
        "3",
        '"C"',
        null,
        null,
        null,
        '"user"',
        '"Jane Doe"',
        "true",
        '"ProjectX"',
        '"ProjectY"',
        null,
        null,
        "null",
        null,
        '"transaction"',
        '"T-98765"',
        "1000000.55",
        '"Laptop"',
        "1",
        "1200",
        null,
        '"Software License"',
        "10",
        "5.5",
        null,
        null,
        '"COMPLETED"',
        null,
        null,
        null,
        null,
    ];

    const sources = [];
    JSON.parse(JSON.stringify(json), (_, __, { source }) => {
        sources.push(source);
    });

    sources
        .map(source => (source === undefined ? null : source))
        .forEach((source, index) => {
            expect(source).toBe(expected[index]);
        });
});

test("proto property does not affect prototype", () => {
    const obj = JSON.parse('{"__proto__": []}');
    expect(Object.getPrototypeOf(obj)).toBe(Object.prototype);
    expect(Array.isArray(obj.__proto__)).toBeTrue();
});

test("duplicate __proto__ properties", () => {
    const obj = JSON.parse('{ "__proto__": 1, "__proto__": 2 }');
    expect(obj.__proto__).toBe(2);
});
