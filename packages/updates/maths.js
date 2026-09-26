// maths.js -- a feature pack from the OpenOS update server
// Big numbers, factorials, primes: printed by OpenJS inside the kernel.

print("MATHS PACK -- installed via OpenOS update");
print("");

print("powers of two:");
var p = 1;
for (var i = 1; i <= 62; i++) {
    p = p * 2;
    if (i == 10 || i == 20 || i == 32 || i == 48 || i == 62) {
        print("2^" + i + " = " + p);
    }
}
print("");

function fact(n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
print("factorials:");
for (var i = 10; i <= 20; i = i + 5) {
    print(i + "! = " + fact(i));
}
print("");

print("the first 15 primes:");
var found = 0;
var n = 2;
var line = "";
while (found < 15) {
    var isPrime = 1;
    for (var d = 2; d * d <= n; d++) {
        if (n % d == 0) { isPrime = 0; }
    }
    if (isPrime) {
        found++;
        line = line + n + " ";
    }
    n++;
}
print(line);
print("");
print("(that is " + fact(13) / fact(11) + " = 13!/11!, just for fun)");
print("done!");
