// art.js -- a feature pack from the OpenOS update server
// ASCII art + a cheeky bit of maths, via OpenJS.

print("  ____                 ___  ____  ");
print(" / __ \\__ _____ ___   / _ \\/ __ \\ ");
print("/ /_/ / // / -_|_-<  / ___/ /_/ / ");
print("\\____/\\_,_/\\__/___/ /_/  \\____/  ");
print("        64-bit, from scratch");
print("");

print("loading art pack features...");
var total = 0;
for (var i = 1; i <= 100; i++) {
    total = total + i;
}
print("gauss says 1+2+...+100 = " + total);
print("");

function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
var line = "";
for (var i = 1; i <= 15; i++) {
    line = line + fib(i) + " ";
}
print("fibonacci: " + line);
print("");
print("art pack installed OK!");
