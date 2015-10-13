var codify = module.exports = {};

// This function accepts numbers from 0-35
function character(num) {
    if (num < 10) return String(num);
    return String.fromCharCode(num-10+65);
}

/*
 * Generate an alphanumeric (base-36) code from an integer
 */
codify.toCode = function(val, minSize) {
    minSize = minSize || 1;
    var code = '';
    while (val >= 1) {
        var remainder = val % 36;
        val = Math.floor(val / 36);
        code = character(remainder)+code;
    }
    while (code.length < minSize) {
        code = '0'+code;
    }
    return code;
};

var digits = {};
for (var i = 0; i < 10; i++) {
    digits[String(i)] = i;
}
for (i = 0; i < 26; i++) {
    digits[String.fromCharCode(i+65)] = i+10;
}

/*
 * Convert an alphanumeric (base-36) code to an integer
 */
codify.toInt = function(code) {
    var calculated = 0;
    for (var i =0; i < code.length; i++) {
        var num = digits[code.charAt(i)];
        calculated += Math.pow(36, code.length-i-1)*num;
    }
    return calculated;
};
