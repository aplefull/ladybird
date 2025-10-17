describe("errors", () => {
    test("invalid pattern", () => {
        expect(() => {
            RegExp("[");
        }).toThrowWithMessage(
            SyntaxError,
            "RegExp compile error: Error during parsing of regular expression:"
        );
    });

    test("invalid flag", () => {
        expect(() => {
            RegExp("", "x");
        }).toThrowWithMessage(SyntaxError, "Invalid RegExp flag 'x'");
    });

    test("repeated flag", () => {
        expect(() => {
            RegExp("", "gg");
        }).toThrowWithMessage(SyntaxError, "Repeated RegExp flag 'g'");
    });
});

test("basic functionality", () => {
    expect(RegExp().toString()).toBe("/(?:)/");
    expect(RegExp(undefined).toString()).toBe("/(?:)/");
    expect(RegExp("foo").toString()).toBe("/foo/");
    expect(RegExp("foo", undefined).toString()).toBe("/foo/");
    expect(RegExp("foo", "g").toString()).toBe("/foo/g");
    expect(RegExp(undefined, "g").toString()).toBe("/(?:)/g");
});

test("regexp object as pattern parameter", () => {
    expect(RegExp(/foo/).toString()).toBe("/foo/");
    expect(RegExp(/foo/g).toString()).toBe("/foo/g");
    expect(RegExp(/foo/g, "").toString()).toBe("/foo/");
    expect(RegExp(/foo/g, "y").toString()).toBe("/foo/y");

    var regex_like_object_without_flags = {
        source: "foo",
        [Symbol.match]: function () {},
    };
    expect(RegExp(regex_like_object_without_flags).toString()).toBe("/foo/");
    expect(RegExp(regex_like_object_without_flags, "y").toString()).toBe("/foo/y");

    var regex_like_object_with_flags = {
        source: "foo",
        flags: "g",
        [Symbol.match]: function () {},
    };
    expect(RegExp(regex_like_object_with_flags).toString()).toBe("/foo/g");
    expect(RegExp(regex_like_object_with_flags, "").toString()).toBe("/foo/");
    expect(RegExp(regex_like_object_with_flags, "y").toString()).toBe("/foo/y");
});

test("regexp literals are re-useable", () => {
    for (var i = 0; i < 2; ++i) {
        const re = /test/;
        expect(re.test("te")).toBeFalse();
        expect(re.test("test")).toBeTrue();
    }
});

test("Incorrectly escaped code units not converted to invalid patterns", () => {
    const re = /[\⪾-\⫀]/;
    expect(re.test("⫀")).toBeTrue();
    expect(re.test("\\u2abe")).toBeFalse(); // ⫀ is \u2abe
});

test("regexp that always matches stops matching if it's past the end of the string instead of infinitely looping", () => {
    const re = new RegExp("[\u200E]*", "gu");
    expect("whf".match(re)).toEqual(["", "", "", ""]);
    expect(re.lastIndex).toBe(0);
});

test("v flag should enable unicode mode", () => {
    const re = new RegExp("a\\u{10FFFF}", "v");
    expect(re.test("a\u{10FFFF}")).toBe(true);
});

test("parsing a large bytestring shouldn't crash", () => {
    RegExp(new Uint8Array(0x40000));
});

test("Unicode non-ASCII matching", () => {
    const cases = [
        { pattern: /é/u, match: "é", expected: ["é"] },
        { pattern: /é/, match: "é", expected: ["é"] },
        { pattern: /\u{61}/u, match: "a", expected: ["a"] },
        { pattern: /\u{61}/, match: "a", expected: null },
        { pattern: /😄/u, match: "😄", expected: ["😄"] },
        { pattern: /😄/u, match: "\ud83d", expected: null },
        { pattern: /😄/, match: "\ud83d", expected: null },
    ];
    for (const test of cases) {
        const result = test.match.match(test.pattern);
        expect(result).toEqual(test.expected);
    }
});

test("Unicode with v/u flags", () => {
    const text = "𠮷a𠮷b𠮷";
    const complexText = "a\u{20BB7}b\u{10FFFF}c";

    const cases = [
        { pattern: /𠮷/, match: text, expected: ["𠮷"] },
        { pattern: /𠮷/u, match: text, expected: ["𠮷"] },
        { pattern: /𠮷/v, match: text, expected: ["𠮷"] },
        { pattern: /\p{Script=Han}/u, match: text, expected: ["𠮷"] },
        { pattern: /\p{Script=Han}/v, match: text, expected: ["𠮷"] },
        { pattern: /./u, match: text, expected: ["𠮷"] },
        { pattern: /./v, match: text, expected: ["𠮷"] },
        { pattern: /\p{ASCII}/u, match: text, expected: ["a"] },
        { pattern: /\p{ASCII}/v, match: text, expected: ["a"] },
        { pattern: /x/u, match: text, expected: null },
        { pattern: /x/v, match: text, expected: null },
        { pattern: /\p{Script=Han}(.)/gu, match: text, expected: ["𠮷a", "𠮷b"] },
        { pattern: /\p{Script=Han}(.)/gv, match: text, expected: ["𠮷a", "𠮷b"] },
        { pattern: /\P{ASCII}/u, match: complexText, expected: ["\u{20BB7}"] },
        { pattern: /\P{ASCII}/v, match: complexText, expected: ["\u{20BB7}"] },
        { pattern: /\P{ASCII}/gu, match: complexText, expected: ["\u{20BB7}", "\u{10FFFF}"] },
        { pattern: /\P{ASCII}/gv, match: complexText, expected: ["\u{20BB7}", "\u{10FFFF}"] },
        { pattern: /./gu, match: text, expected: ["𠮷", "a", "𠮷", "b", "𠮷"] },
        { pattern: /./gv, match: text, expected: ["𠮷", "a", "𠮷", "b", "𠮷"] },
        { pattern: /(?:)/gu, match: text, expected: ["", "", "", "", "", ""] },
        { pattern: /(?:)/gv, match: text, expected: ["", "", "", "", "", ""] },
    ];

    for (const test of cases) {
        const result = test.match.match(test.pattern);
        expect(result).toEqual(test.expected);
    }
});
