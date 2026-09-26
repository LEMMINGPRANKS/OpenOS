// OpenJS demo -- run me with: run demo.js

print("hello from OpenJS inside OpenOS!");

var total = 0;
for (var i = 1; i <= 5; i++) {
    total = total + i * i;
}
print("sum of the first 5 squares:", total);

function fact(n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
print("10! = " + fact(10));

var word = "OpenOS";
var shout = word + " " + word + "!";
print(shout);

var i_am_still = "here";
print("done.", i_am_still);
