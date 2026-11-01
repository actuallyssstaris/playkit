/*
 * JavaScriptCore smoke test for the PS5 build.
 *
 * Runs a script that touches the parts of the engine a browser depends on:
 * numbers, strings with non-ASCII, arrays, regexp, JSON, typed arrays,
 * BigInt, Date, and Intl.  Prints one line per check so a serial log is
 * enough to see where it stops.
 */

#include <JavaScriptCore/JavaScript.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int failures;

static const char *source =
    "var out = [];\n"
    "function check(name, value, expected) {\n"
    "  out.push((name + ': ' + (value === expected ? 'ok' : 'FAIL ' + value + ' != ' + expected)));\n"
    "}\n"
    "check('arith', (1 + 2) * 3, 9);\n"
    "check('string', 'héllo'.length, 5);\n"
    "check('unicode', '\\u{1F600}'.codePointAt(0), 0x1F600);\n"
    "check('array', [3, 1, 2].sort().join(','), '1,2,3');\n"
    "check('regexp', /a(b+)c/.exec('abbc')[1], 'bb');\n"
    "check('json', JSON.parse('{\"a\":[1,2]}').a[1], 2);\n"
    "check('typed', new Uint32Array([7])[0], 7);\n"
    "check('bigint', (2n ** 64n).toString(), '18446744073709551616');\n"
    "check('date', new Date(0).getUTCFullYear(), 1970);\n"
    "check('intl', new Intl.NumberFormat('en-US').format(1234.5), '1,234.5');\n"
    "out.join('\\n');\n";

int main(void)
{
    JSGlobalContextRef context;
    JSStringRef script, line;
    JSValueRef result, exception;
    size_t length;
    char *chars;
    int failed = 0;

    context = JSGlobalContextCreate(NULL);
    if (!context) {
        printf("FAIL context\n");
        return 1;
    }

    script = JSStringCreateWithUTF8CString(source);
    result = JSEvaluateScript(context, script, NULL, NULL, 1, &exception);
    JSStringRelease(script);

    if (!result) {
        JSStringRef text = JSValueToStringCopy(context, exception, NULL);
        size_t n = JSStringGetMaximumUTF8CStringSize(text);
        char *buffer = (char *)malloc(n);
        JSStringGetUTF8CString(text, buffer, n);
        printf("FAIL exception: %s\n", buffer);
        free(buffer);
        JSStringRelease(text);
        JSGlobalContextRelease(context);
        return 1;
    }

    line = JSValueToStringCopy(context, result, NULL);
    length = JSStringGetMaximumUTF8CStringSize(line);
    chars = (char *)malloc(length);
    JSStringGetUTF8CString(line, chars, length);
    printf("%s\n", chars);

    for (const char *p = chars; *p; p++) {
        if (p[0] == 'F' && strncmp(p, "FAIL", 4) == 0)
            failed = 1;
    }

    printf("result: %s\n", failed ? "FAIL" : "PASS");

    free(chars);
    JSStringRelease(line);
    JSGlobalContextRelease(context);
    return failed;
}
