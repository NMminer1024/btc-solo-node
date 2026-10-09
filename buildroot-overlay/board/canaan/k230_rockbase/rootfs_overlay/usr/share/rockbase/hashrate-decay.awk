# ckpool updates pool averages every 1.875s using 1 / (2 - exp(-dt/window)).
# BusyBox awk has no math functions on Rockbase. For dt/window <= 0.03125,
# eight Taylor terms reproduce exp(-x) to double precision.
function factor(dt, window,    x, term, sum, i) {
    x = -dt / window
    term = sum = 1
    for (i = 1; i <= 8; i++) {
        term *= x / i
        sum += term
    }
    return 1 / (2 - sum)
}

function power(base, n,    result) {
    result = 1
    while (n > 0) {
        if (n % 2 == 1) result *= base
        base *= base
        n = int(n / 2)
    }
    return result
}

BEGIN {
    windows[1] = 60
    windows[2] = 300
    windows[3] = 900
    windows[4] = 3600
    windows[5] = 21600
    windows[6] = 86400
    windows[7] = 604800
    scale["K"] = 0.000001
    scale["M"] = 0.001
    scale["G"] = 1
    scale["T"] = 1000
    scale["P"] = 1000000
    scale["E"] = 1000000000
}

{
    if (NR > 7 || $0 !~ /^([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[KMGTPE]?$/) {
        print "Invalid hashrate for decay: " $0 > "/dev/stderr"
        failed = 1
        exit 1
    }
    suffix = substr($0, length($0), 1)
    value = ($0 + 0) * (suffix in scale ? scale[suffix] : 1)
    if (age > 0) {
        steps = int(age / 1.875)
        value *= power(factor(1.875, windows[NR]), steps)
        value *= factor(age - steps * 1.875, windows[NR])
    }
    # Below one hash/s is not a meaningful window estimate.
    if (value < 0.000000001) print "0"
    else if (value >= 1000000) printf "%.6gP\n", value / 1000000
    else if (value >= 1000) printf "%.6gT\n", value / 1000
    else printf "%.6gG\n", value
}

END {
    if (!failed && NR != 7) {
        print "Expected seven hashrate windows" > "/dev/stderr"
        exit 1
    }
}
