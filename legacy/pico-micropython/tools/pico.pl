#!/usr/bin/perl
# Tiny MicroPython raw-REPL client (no Python needed).
# Usage:
#   pico.pl ls
#   pico.pl exec 'print(1+1)'
#   pico.pl get REMOTE LOCAL
#   pico.pl put LOCAL REMOTE
#   pico.pl run          (soft reset -> board runs main.py)
use strict;
use warnings;
use POSIX qw(:termios_h);
use Fcntl;
use IO::Select;
use MIME::Base64;
use Time::HiRes qw(time sleep);

my $port = $ENV{PORT} || (glob('/dev/cu.usbmodem*'))[0] or die "No /dev/cu.usbmodem* port found\n";
sysopen(my $fh, $port, O_RDWR | O_NOCTTY | O_NONBLOCK) or die "open $port: $!\n";
my $fd = fileno($fh);
my $t = POSIX::Termios->new;
$t->getattr($fd);
$t->setlflag(0);
$t->setiflag(0);
$t->setoflag(0);
my $c = $t->getcflag;
$c &= ~(CSIZE | PARENB);
$c |= CS8 | CREAD | CLOCAL;
$t->setcflag($c);
$t->setcc(VMIN, 0);
$t->setcc(VTIME, 0);
$t->setattr($fd, TCSANOW);
my $sel = IO::Select->new($fh);

sub wr {
    my ($data) = @_;
    while (length $data) {
        my $chunk = substr($data, 0, 256, '');
        while (length $chunk) {
            my $n = syswrite($fh, $chunk);
            if (!defined $n) { sleep 0.005; next; }
            substr($chunk, 0, $n, '');
        }
        sleep 0.01;
    }
}

# Read until $re matches the accumulated buffer, or die after $timeout seconds.
sub read_until {
    my ($re, $timeout) = @_;
    my $buf = '';
    my $end = time + $timeout;
    while (1) {
        return $buf if $buf =~ $re;
        my $left = $end - time;
        die "Timed out waiting for board. Got so far:\n$buf\n" if $left <= 0;
        if ($sel->can_read($left > 0.2 ? 0.2 : $left)) {
            my $n = sysread($fh, my $in, 4096);
            $buf .= $in if $n;
        }
    }
}

sub drain {
    my ($secs) = @_;
    my $buf = '';
    my $end = time + $secs;
    while (time < $end) {
        if ($sel->can_read(0.05)) { my $n = sysread($fh, my $in, 4096); $buf .= $in if $n; }
    }
    return $buf;
}

sub enter_raw {
    wr("\r\x03");
    sleep 0.1;
    wr("\x03");
    sleep 0.1;
    drain(0.3);
    wr("\r\x01");
    read_until(qr/raw REPL; CTRL-B to exit\r\n>/, 5);
}

sub rexec {
    my ($code, $timeout) = @_;
    wr($code);
    wr("\x04");
    my $buf = read_until(qr/OK.*?\x04.*?\x04>/s, $timeout || 20);
    $buf =~ /OK(.*?)\x04(.*?)\x04>/s;
    my ($out, $err) = ($1, $2);
    die "Board error:\n$err\n" if length $err;
    return $out;
}

my $cmd = shift @ARGV or die "usage: pico.pl ls|exec|get|put|run\n";

if ($cmd eq 'run') {
    wr("\r\x03\x03");
    sleep 0.1;
    wr("\x02");    # leave raw REPL if in it
    sleep 0.1;
    drain(0.2);
    wr("\x04");    # soft reset -> runs main.py
    print drain(2);
    exit 0;
}

enter_raw();

if ($cmd eq 'ls') {
    print rexec("import os\nfor f in os.ilistdir():\n print(f[0], 'dir' if f[1]==0x4000 else os.stat(f[0])[6])\n");
}
elsif ($cmd eq 'exec') {
    print rexec(join("\n", @ARGV) . "\n", $ENV{TIMEOUT} || 60);
}
elsif ($cmd eq 'get') {
    my ($remote, $local) = @ARGV;
    my $out = rexec("import binascii\nwith open('$remote','rb') as f:\n while True:\n  b=f.read(240)\n  if not b: break\n  print(binascii.b2a_base64(b).decode().strip())\n", 60);
    my $data = join '', map { decode_base64($_) } grep { length } split /\r?\n/, $out;
    open(my $o, '>:raw', $local) or die "write $local: $!\n";
    print $o $data;
    close $o;
    print "got $remote -> $local (" . length($data) . " bytes)\n";
}
elsif ($cmd eq 'put') {
    my ($local, $remote) = @ARGV;
    open(my $i, '<:raw', $local) or die "read $local: $!\n";
    my $data = do { local $/; <$i> };
    close $i;
    rexec("import binascii\nf=open('$remote','wb')\nw=lambda s:f.write(binascii.a2b_base64(s))\n");
    my @lines;
    for (my $p = 0; $p < length $data; $p += 192) {
        push @lines, "w('" . encode_base64(substr($data, $p, 192), '') . "')";
    }
    while (@lines) {
        rexec(join("\n", splice(@lines, 0, 8)) . "\n");
    }
    rexec("f.close()\n");
    my $size = rexec("import os\nprint(os.stat('$remote')[6])\n");
    $size =~ s/\s+//g;
    die "Size mismatch: local " . length($data) . " vs board $size\n" if $size != length $data;
    print "put $local -> $remote ($size bytes, verified)\n";
}
else {
    die "unknown command $cmd\n";
}

wr("\x02");    # back to normal REPL
close $fh;
