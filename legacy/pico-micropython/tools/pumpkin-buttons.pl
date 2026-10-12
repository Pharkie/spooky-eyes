#!/usr/bin/perl
# Pumpkin Buttons: a little web page with a button for each effect.
#
#   perl pumpkin-buttons.pl      then open http://localhost:8765
#
# Pressing a button pauses the normal show and plays that effect on the eyes; a new press
# interrupts whatever is playing. With "Talking pumpkin" on, the Mac's voice joins in.
# "Back to the normal show" restarts main.py. Only uses Perl's built-in modules.
use strict;
use warnings;
use POSIX qw(:termios_h);
use Fcntl;
use IO::Select;
use IO::Socket::INET;
use MIME::Base64;
use Time::HiRes qw(time sleep);

$| = 1;
my $HTTP_PORT = $ENV{HTTP_PORT} || 8765;

# PUMPKIN_PHONE=1 lets phones on the same WiFi use the buttons too (the launcher turns this on)
my $PHONE = $ENV{PUMPKIN_PHONE};
my ($LAN_IP) = grep { /^\d+\.\d+\.\d+\.\d+$/ } map { my $ip = `ipconfig getifaddr $_ 2>/dev/null`; chomp $ip; $ip } qw(en0 en1 en2 en3);
my $PHONE_URL = $PHONE && $LAN_IP ? "http://$LAN_IP:$HTTP_PORT" : '';

# What each button runs on the board: the same calls as the loop in main.py
my %EFFECTS = (
    minecraft => "m.anim_runner(a, 'minecraft', E)",
    pumpkin   => "m.flip(S['pumpkin1'], S['pumpkin2'], 6, 0.4)",
    invaders  => "m.flip(S['invader1'], S['invader2'], 8, 0.3)",
    trees     => "m.flip(S['tree1'], S['tree1lit2'], 6, 0.4)",
    bunnies   => "m.flip(S['bunny1'], S['bunny3'], 8, 0.25)",
    witch     => "m.scroll_message(f.witch, ' 12 ')",
    ghosts    => "m.show_char(E['ghost1'], E['ghost2'])\ntime.sleep(1)",
    hearts    => "m.flip(S['heart1'], S['heart2'], 3, 0.5)",
    blink     => "m.anim_runner(a, 'stareAndBlink', E)",
    winkleft  => "m.anim_runner(a, 'winkLeft', E)",
    winkright => "m.anim_runner(a, 'winkRight', E)",
    roll      => "m.anim_runner(a, 'roll', E)",
    lookdown  => "m.anim_runner(a, 'downLeftABit', E)",
    grow      => "m.anim_runner(a, 'growEyes', E)",
    name      => "m.scroll_message(T, \" Jack's Pumpkin \")",
    spell     => "m.scroll_message(T, ' Double, double toil and trouble Fire burn and caldron bubble ', 0.03)",
    trick     => "m.scroll_message(T, ' Trick or Treat? ', 0.02)",
    halloween => "m.scroll_message(T, ' Happy Halloween! ', 0.03)",
    bat       => "m.fly(f.bat, (' a ', ' b '))",
    spider    => "m.spider_drop()",
    pacman    => "m.fly(f.pacman, (' g o ', ' h c '), 0.04, 4)",
    pong      => "m.bounce()",
    star      => "m.star_spin()",
    skull     => "m.flip(S['skull1'], S['skull2'], 10, 0.15)",
    cat       => "m.cat_blink()",
    angry     => "m.angry_eyes()",
    sleepy    => "m.sleepy_eyes()",
    crosseyed => "m.cross_eyed()",
    creeper   => "m.creeper_explode()",
    diamond   => "m.flip(S['diamond1'], S['diamond2'], 8, 0.3)",
    sword     => "m.flip(S['sword1'], S['sword2'], 8, 0.2)",
    enderman  => "m.enderman()",
    dice      => "m.roll_dice()",
    magic     => "m.magic_answer()",
    boo       => "m.countdown_boo()",
    emerald   => "m.emerald_trade()",
    panda     => "m.panda()",
    mining    => "m.mine()",
    dig       => "m.dig()",
    chop      => "m.chop_tree()",
    golem     => "m.iron_golem()",
    lamp      => "m.redstone()",
    tnt       => "m.redstone(True)",
    ghast     => "m.ghast_fireball()",
    skeleton  => "m.skeleton_arrow()",
    slime     => "m.slime_bounce()",
    daynight  => "m.day_and_night()",
    portal    => "m.nether_portal()",
);

# Surprise show: keeps picking random effects (not the long spell) until another button is pressed
my @MIX = sort grep { $_ ne 'spell' } keys %EFFECTS;
my $surprise = "import random\nwhile True:\n n = random.randrange(" . scalar(@MIX) . ")\n";
for my $i (0 .. $#MIX) {
    $surprise .= ($i ? ' elif' : ' if') . " n == $i:\n  print('FX:$MIX[$i]')\n";
    $surprise .= join '', map { "  $_\n" } split /\n/, $EFFECTS{$MIX[$i]};
}
$EFFECTS{surprise} = $surprise . " time.sleep(0.5)";

# What the talking pumpkin says (creeper, dice, magic and boo also speak from the board with SAY: lines)
my %PHRASES = (
    minecraft => 'Minecraft eyes!',
    pumpkin   => 'Chomp chomp chomp!',
    invaders  => 'The space invaders are coming!',
    trees     => 'Merry Christmas!',
    bunnies   => 'Hop hop hop!',
    witch     => 'Hee hee hee! Here I come on my broomstick!',
    ghosts    => 'Woooooooo!',
    hearts    => 'I love you!',
    blink     => 'Blink blink.',
    winkleft  => 'Wink!',
    winkright => 'Wink wink!',
    roll      => 'Whatever!',
    lookdown  => "What's down there?",
    grow      => 'Whoa!',
    name      => "The pumpkin's name!",
    spell     => 'Double, double, toil and trouble. Fire burn, and cauldron bubble!',
    trick     => 'Trick or treat!',
    halloween => 'Happy Halloween!',
    bat       => "Squeak squeak! I'm a bat!",
    spider    => 'Eek! A spider!',
    pacman    => 'Waka waka waka waka!',
    pong      => 'Boing! Boing! Boing!',
    star      => 'Twinkle twinkle, little star!',
    skull     => 'Rattle rattle rattle!',
    cat       => 'Meow!',
    angry     => "Grrrr! I'm really cross!",
    sleepy    => "I'm so sleepy. Zzzzzzzz.",
    crosseyed => "Ooh, I'm going cross-eyed!",
    diamond   => 'Diamonds!',
    sword     => 'Swish! Swoosh!',
    enderman  => "Don't look at the Enderman!",
    dice      => 'Rolling the dice!',
    magic     => 'Hmm, let me think.',
    surprise  => 'Surprise!',
    panda     => "I'm a hungry panda. Munch, munch, munch!",
    mining    => "Let's go mining!",
    dig       => 'Dig, dig, dig!',
    chop      => 'Look, a little sapling is growing!',
    golem     => "Hello. I'm an iron golem.",
    lamp      => 'Redstone power!',
    tnt       => 'Uh oh. Redstone, and TNT!',
    ghast     => 'Watch out, a ghast!',
    skeleton  => 'A skeleton with a bow and arrow!',
    portal    => 'The Nether portal is opening!',
);

# ---- Talking, using the Mac's voice ----

# The Mac's English voices, for the voice picker (Zarvox was the favourite)
my @VOICES = sort grep { length } map { /^(.+?)\s+en_\w+\s+#/ ? $1 : '' } `say -v '?'`;
my %VOICE_OK = map { $_ => 1 } @VOICES;
my $DEFAULT_VOICE = $VOICE_OK{Zarvox} ? 'Zarvox' : '';

my $say_pid;
$SIG{CHLD} = 'IGNORE';

sub stop_speaking {
    kill 'TERM', $say_pid if $say_pid;
    undef $say_pid;
}

sub speak {
    my ($words, $voice) = @_;
    stop_speaking();
    my $pid = fork;
    return unless defined $pid;
    if ($pid == 0) { { exec('say', ($voice ? ('-v', $voice) : ()), $words) }; POSIX::_exit(0); }
    $say_pid = $pid;
}

# ---- Serial link to the board (MicroPython raw REPL) ----

my ($fh, $sel);
my $ready = 0;    # 1 once the board is paused with main.py's functions loaded

sub open_port {
    my $port = (glob('/dev/cu.usbmodem*'))[0] or die "Can't find the pumpkin. Is the USB cable plugged in?\n";
    sysopen($fh, $port, O_RDWR | O_NOCTTY | O_NONBLOCK) or die "Can't open $port: $!\n";
    my $t = POSIX::Termios->new;
    $t->getattr(fileno $fh);
    $t->setlflag(0);
    $t->setiflag(0);
    $t->setoflag(0);
    my $c = $t->getcflag;
    $c &= ~(CSIZE | PARENB);
    $c |= CS8 | CREAD | CLOCAL;
    $t->setcflag($c);
    $t->setcc(VMIN, 0);
    $t->setcc(VTIME, 0);
    $t->setattr(fileno $fh, TCSANOW);
    $sel = IO::Select->new($fh);
}

sub close_port {
    close $fh if $fh;
    undef $fh;
    $ready = 0;
}

sub wr {
    my ($data) = @_;
    while (length $data) {
        my $chunk = substr($data, 0, 256, '');
        while (length $chunk) {
            my $n = syswrite($fh, $chunk);
            if (!defined $n) {
                die "Lost the pumpkin. Was the USB cable unplugged?\n" unless $!{EAGAIN};
                sleep 0.005;
                next;
            }
            substr($chunk, 0, $n, '');
        }
        sleep 0.01;
    }
}

sub read_until {
    my ($re, $timeout) = @_;
    my $buf = '';
    my $end = time + $timeout;
    while ($buf !~ $re) {
        my $left = $end - time;
        die "The pumpkin didn't answer in time.\n" if $left <= 0;
        if ($sel->can_read($left > 0.2 ? 0.2 : $left)) {
            my $n = sysread($fh, my $in, 4096);
            die "Lost the pumpkin. Was the USB cable unplugged?\n" if defined $n && $n == 0;
            $buf .= $in if $n;
        }
    }
    return $buf;
}

sub drain {
    my ($secs) = @_;
    my $end = time + $secs;
    while (time < $end) {
        sysread($fh, my $in, 4096) if $sel->can_read(0.05);
    }
}

# Run code on the board and wait for it to finish. Python errors die with "Board error".
sub rexec {
    my ($code, $timeout) = @_;
    wr($code . "\n\x04");
    my $buf = read_until(qr/OK.*?\x04.*?\x04>/s, $timeout || 20);
    $buf =~ /OK(.*?)\x04(.*?)\x04>/s;
    die "Board error: $2\n" if length $2;
    return $1;
}

# Pause the normal show and load main.py's functions, ready for buttons
sub get_ready {
    return if $ready;
    open_port() unless $fh;
    wr("\r\x03");
    sleep 0.1;
    wr("\x03");
    sleep 0.1;
    drain(0.3);
    wr("\r\x01");
    read_until(qr/raw REPL; CTRL-B to exit\r\n>/, 5);
    wr("\x04");    # soft reset for a clean start (main.py doesn't run in raw REPL)
    read_until(qr/raw REPL; CTRL-B to exit\r\n>/, 10);
    rexec("import time, binascii\nimport main as m, matrix_fonts as f\na = m.load_anims('eyes_ani.json')\nS = f.shapes\nE = f.eyes\nT = f.textFont1");
    $ready = 1;
}

sub resume {
    eval {
        open_port() unless $fh;
        wr("\r\x03\x03");
        sleep 0.1;
        wr("\x02");    # leave raw REPL
        sleep 0.1;
        drain(0.2);
        wr("\x04");    # soft reset in the normal REPL runs main.py
        drain(0.5);
        1;
    } or do { my $e = $@; close_port(); die $e; };
    $ready = 0;
}

$SIG{INT} = $SIG{TERM} = $SIG{HUP} = sub {    # HUP: the Terminal window was closed
    stop_speaking();
    eval { resume() if $ready };
    exit 0;
};
$SIG{PIPE} = 'IGNORE';    # a page closed before we answered shouldn't stop the server

# ---- Web server ----

sub read_request {
    my ($c) = @_;
    my $s = IO::Select->new($c);
    my $buf = '';
    my $end = time + 5;
    my $more = sub {
        my $left = $end - time;
        return 0 if $left <= 0 || !$s->can_read($left);
        my $n = sysread($c, my $in, 8192);
        return 0 unless $n;
        $buf .= $in;
        return 1;
    };
    while ($buf !~ /\r\n\r\n/) { return unless $more->(); }
    my ($head, $body) = split /\r\n\r\n/, $buf, 2;
    my ($method, $path) = $head =~ m{^(\w+)\s+(\S+)};
    my ($len) = $head =~ /^content-length:\s*(\d+)/im;
    $len = 0 unless $len;
    $len = 10000 if $len > 10000;
    while (length($body) < $len) {
        last unless $more->();
        (undef, $body) = split /\r\n\r\n/, $buf, 2;
    }
    return ($method, $path, substr($body, 0, $len));
}

sub respond {
    my ($c, $status, $type, $content) = @_;
    print $c "HTTP/1.0 $status\r\nContent-Type: $type\r\nContent-Length: " . length($content)
        . "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n$content";
}

sub reply {
    my ($c, $status, $msg) = @_;
    respond($c, $status, 'text/plain; charset=utf-8', $msg);
    close $c;
}

# The effect playing on the board right now, and at most one action waiting to run next
my $playing = 0;
my ($play_client, $play_buf, $play_lines, $play_talk, $play_voice, $play_started, $play_endless, $play_done_msg);
my @queue;

sub start_action {
    my ($act) = @_;
    stop_speaking();
    if ($act->{type} eq 'resume') {
        print "Back to the normal show\n";
        if (eval { resume(); 1 }) { reply($act->{client}, '200 OK', 'The normal show is running again.'); }
        else { reply($act->{client}, '500 Problem', $@); }
        return;
    }
    print "Playing $act->{name}\n";
    if (!eval { get_ready(); wr($act->{code} . "\n\x04"); 1 }) {
        my $e = $@;
        close_port();
        reply($act->{client}, '500 Problem', $e);
        return;
    }
    speak($act->{phrase}, $act->{voice}) if $act->{talk} && $act->{phrase};
    $playing = 1;
    ($play_buf, $play_lines, $play_talk, $play_voice, $play_started) = ('', 0, $act->{talk}, $act->{voice}, time);
    ($play_endless, $play_done_msg) = ($act->{endless}, $act->{done});
    if ($play_endless) {
        reply($act->{client}, '200 OK', $act->{done});    # it never finishes by itself, so answer now
        undef $play_client;
    }
    else {
        $play_client = $act->{client};
    }
}

# Run an action now, or if something is playing, stop it (Ctrl-C) and run this next
sub request_action {
    my ($act) = @_;
    return start_action($act) unless $playing;
    if (@queue) {
        reply($queue[0]{client}, '200 OK', 'Skipped');
        @queue = ($act);
        return;
    }
    @queue = ($act);
    if (!eval { wr("\x03"); 1 }) {
        my $e = $@;
        close_port();
        finish_playing('500 Problem', $e);
    }
}

sub finish_playing {
    my ($status, $msg) = @_;
    $playing = 0;
    reply($play_client, $status, $msg) if $play_client;
    undef $play_client;
    start_action(shift @queue) if @queue;
}

# Speak any SAY: lines (and FX: names from the surprise show) the board has printed so far
sub board_lines {
    my $start = index($play_buf, 'OK');
    return if $start < 0;
    my $out = substr($play_buf, $start + 2);
    my $end = index($out, "\x04");
    $out = substr($out, 0, $end) . "\n" if $end >= 0;
    my @lines = split /\r?\n/, $out, -1;
    pop @lines;    # the last piece isn't a finished line yet
    for my $line (@lines[$play_lines .. $#lines]) {
        next unless $play_talk;
        if ($line =~ /^SAY:(.*)/) { speak($1, $play_voice); }
        elsif ($line =~ /^FX:(\w+)/ && $PHRASES{$1}) { speak($PHRASES{$1}, $play_voice); }
    }
    $play_lines = @lines;
}

sub serial_ready {
    my $n = sysread($fh, my $in, 4096);
    if (defined $n && $n == 0) {
        close_port();
        return finish_playing('500 Problem', "Lost the pumpkin. Was the USB cable unplugged?\n");
    }
    return unless $n;
    $play_buf .= $in;
    board_lines();
    return unless $play_buf =~ /OK(.*?)\x04(.*?)\x04>/s;
    my $err = $2;
    if ($err =~ /KeyboardInterrupt/) { finish_playing('200 OK', 'Stopped'); }
    elsif (length $err) { finish_playing('500 Problem', "Board error: $err"); }
    else { finish_playing('200 OK', $play_done_msg); }
}

my $page = do { local $/; <DATA> };
my $voice_options = join '', map { "<option" . ($_ eq $DEFAULT_VOICE ? ' selected' : '') . ">$_</option>" } @VOICES;
$page =~ s/<!--VOICES-->/$voice_options/;
my $phone_block = $PHONE_URL ? <<"HTML" : '';
<div class="phone">📱 On a phone on the same WiFi, open <b>$PHONE_URL</b> or scan this:<div id="qr"></div></div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/qrcodejs/1.0.0/qrcode.min.js"></script>
<script>try { new QRCode(document.getElementById('qr'), { text: '$PHONE_URL', width: 150, height: 150 }); } catch (e) {}</script>
HTML
$page =~ s/<!--PHONE-->/$phone_block/;

sub handle_connection {
    my ($c) = @_;
    my ($method, $target, $body) = read_request($c);
    if (!defined $method) { close $c; return; }
    my ($path, $query) = split /\?/, $target, 2;
    my %q;
    for (split /&/, $query || '') {
        my ($k, $v) = map { my $s = $_ // ''; $s =~ tr/+/ /; $s =~ s/%([0-9A-Fa-f]{2})/chr hex $1/ge; $s } split /=/, $_, 2;
        $q{$k} = $v;
    }
    my $talk = ($q{talk} || '') eq '1';
    my $voice = $VOICE_OK{$q{voice} || ''} ? $q{voice} : $DEFAULT_VOICE;
    if ($method eq 'GET' && $path eq '/') {
        respond($c, '200 OK', 'text/html; charset=utf-8', $page);
        close $c;
    }
    elsif ($method eq 'POST' && $path =~ m{^/play/(\w+)$} && $EFFECTS{$1}) {
        my $name = $1;
        my $endless = $name eq 'surprise';
        request_action({ type => 'code', name => $name, code => $EFFECTS{$name}, client => $c,
            talk => $talk, voice => $voice, phrase => $PHRASES{$name}, endless => $endless,
            done => $endless ? 'Surprise show running! Press any button to stop it.' : 'Done! Press another one.' });
    }
    elsif ($method eq 'POST' && $path eq '/say') {
        my $words = $body;
        $words =~ s/\xe2\x80[\x98\x99]/'/g;    # curly quotes -> plain ones
        $words =~ s/\xe2\x80[\x9c\x9d]/"/g;
        $words =~ s/[^\x20-\x7e]//g;           # the font only has plain letters and symbols
        $words = substr($words, 0, 200);
        return reply($c, '400 Bad Request', "Type some words first\n") unless $words =~ /\S/;
        my $b64 = encode_base64(" $words ", '');
        request_action({ type => 'code', name => "words: $words", client => $c, talk => $talk, voice => $voice, phrase => $words,
            code => "m.scroll_message(T, binascii.a2b_base64('$b64').decode())",
            done => 'Done! Type something else, or press a button.' });
    }
    elsif ($method eq 'POST' && $path eq '/resume') {
        request_action({ type => 'resume', client => $c });
    }
    elsif ($method eq 'POST' && $path eq '/voicetest') {
        speak("Hello! This is my new voice.", $voice);
        reply($c, '200 OK', "Voice: $voice");
    }
    else {
        reply($c, '404 Not Found', 'Not found');
    }
}

my $server = IO::Socket::INET->new(LocalAddr => $PHONE ? '0.0.0.0' : '127.0.0.1', LocalPort => $HTTP_PORT,
    Listen => 10, ReuseAddr => 1)
    or die "Can't start the button page on port $HTTP_PORT: $!\n";
print "Pumpkin buttons ready on http://localhost:$HTTP_PORT\n";
print "On a phone on the same WiFi, open $PHONE_URL\n" if $PHONE_URL;

while (1) {
    my @watch = ($server);
    push @watch, $fh if $playing && $fh;
    for my $h (IO::Select->new(@watch)->can_read(0.2)) {
        if ($h == $server) {
            my $c = $server->accept;
            handle_connection($c) if $c;
        }
        elsif ($playing && $fh && $h == $fh) {
            serial_ready();
        }
    }
    if ($playing && !$play_endless && time - $play_started > 120) {
        close_port();
        finish_playing('500 Problem', "The pumpkin didn't answer in time.\n");
    }
}

__DATA__
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Pumpkin Buttons</title>
<style>
:root { --bg: #1b1324; --card: #2a1d38; --orange: #f28c28; --on-orange: #2a1300; --purple: #9b5de5; --green: #5cb85c; --on-green: #0b2a0b; --blue: #3a86ff; --text: #f7efe3; --muted: #c9b8d9; --yellow: #ffd166; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font-family: system-ui, -apple-system, sans-serif; padding: 16px; }
main { max-width: 900px; margin: 0 auto; }
h1 { font-size: 30px; margin: 8px 0 4px; text-align: center; }
#status { text-align: center; font-size: 20px; min-height: 30px; color: var(--muted); margin-bottom: 8px; }
#status.playing { color: var(--yellow); }
.talk { display: flex; align-items: center; justify-content: center; flex-wrap: wrap; gap: 14px; font-size: 20px; }
.talk label { display: flex; align-items: center; gap: 10px; cursor: pointer; user-select: none; }
.talk input { width: 26px; height: 26px; accent-color: var(--yellow); }
.talk select { font: inherit; font-size: 18px; padding: 6px 10px; border-radius: 10px; border: 2px solid var(--yellow); background: var(--card); color: var(--text); }
h2 { font-size: 18px; color: var(--muted); margin: 22px 0 8px; font-weight: 600; }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 12px; }
button { font: inherit; border: 0; border-radius: 16px; background: var(--orange); color: var(--on-orange); font-size: 18px; font-weight: 600; padding: 14px 10px; cursor: pointer; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 6px; min-height: 100px; transition: transform .08s; }
button .e { font-size: 38px; line-height: 1; }
button:active { transform: scale(.95); }
button.now { outline: 5px solid var(--yellow); }
.mc button { background: var(--green); color: var(--on-green); }
.games button { background: var(--blue); color: #fff; }
.words button { background: var(--purple); color: #fff; }
.own { display: flex; gap: 10px; flex-wrap: wrap; }
.own input { flex: 1; min-width: 200px; font-size: 22px; padding: 14px; border-radius: 12px; border: 2px solid var(--purple); background: var(--card); color: var(--text); }
.own button { min-height: 0; flex-direction: row; background: var(--purple); color: #fff; padding: 14px 22px; }
#err { color: #ff8fa3; min-height: 22px; font-size: 15px; margin-top: 6px; }
.phone { text-align: center; font-size: 17px; color: var(--muted); margin: 16px 0; }
.phone b { color: var(--text); }
#qr { display: flex; justify-content: center; margin-top: 10px; }
#qr img, #qr canvas { background: #fff; padding: 8px; border-radius: 8px; }
#show { background: #06d6a0; color: #00281e; width: 100%; min-height: 0; flex-direction: row; margin: 24px 0 8px; padding: 18px; font-size: 20px; }
</style>
</head>
<body>
<main>
<h1>🎃 Pumpkin Buttons</h1>
<div id="status">Press a button!</div>
<div class="talk">
<label><input type="checkbox" id="talk" checked> 🗣️ Talking pumpkin</label>
<select id="voice" aria-label="Voice"><!--VOICES--></select>
</div>

<h2>Pictures</h2>
<div class="grid">
<button data-fx="pumpkin"><span class="e">🎃</span>Pumpkins</button>
<button data-fx="witch"><span class="e">🧹</span>Witch</button>
<button data-fx="bat"><span class="e">🦇</span>Bat</button>
<button data-fx="spider"><span class="e">🕷️</span>Spider</button>
<button data-fx="ghosts"><span class="e">👻</span>Ghosts</button>
<button data-fx="skull"><span class="e">💀</span>Skull</button>
<button data-fx="cat"><span class="e">🐱</span>Cat</button>
<button data-fx="pacman"><span class="e">🟡</span>Pac-Man</button>
<button data-fx="invaders"><span class="e">👾</span>Space invaders</button>
<button data-fx="pong"><span class="e">🏓</span>Pong</button>
<button data-fx="star"><span class="e">🌟</span>Spinning star</button>
<button data-fx="bunnies"><span class="e">🐰</span>Bunnies</button>
<button data-fx="trees"><span class="e">🎄</span>Christmas trees</button>
<button data-fx="hearts"><span class="e">❤️</span>Hearts</button>
</div>

<h2>Minecraft</h2>
<div class="grid mc">
<button data-fx="minecraft"><span class="e">🟩</span>Minecraft eyes</button>
<button data-fx="creeper"><span class="e">💥</span>Creeper</button>
<button data-fx="diamond"><span class="e">💎</span>Diamond</button>
<button data-fx="sword"><span class="e">🗡️</span>Sword</button>
<button data-fx="enderman"><span class="e">🟪</span>Enderman</button>
<button data-fx="emerald"><span class="e">💚</span>Emerald trade</button>
<button data-fx="panda"><span class="e">🐼</span>Panda</button>
<button data-fx="mining"><span class="e">⛏️</span>Mining</button>
<button data-fx="dig"><span class="e">🪏</span>Shovel</button>
<button data-fx="chop"><span class="e">🪓</span>Chop a tree</button>
<button data-fx="golem"><span class="e">🌹</span>Iron golem</button>
<button data-fx="lamp"><span class="e">💡</span>Redstone lamp</button>
<button data-fx="tnt"><span class="e">🧨</span>Redstone TNT</button>
<button data-fx="ghast"><span class="e">🔥</span>Ghast</button>
<button data-fx="skeleton"><span class="e">🏹</span>Skeleton</button>
<button data-fx="slime"><span class="e">🟢</span>Slime</button>
<button data-fx="daynight"><span class="e">🌙</span>Day and night</button>
<button data-fx="portal"><span class="e">🌀</span>Nether portal</button>
</div>

<h2>Eyes</h2>
<div class="grid">
<button data-fx="blink"><span class="e">👀</span>Stare and blink</button>
<button data-fx="winkleft"><span class="e">😉</span>Wink left</button>
<button data-fx="winkright"><span class="e">😜</span>Wink right</button>
<button data-fx="roll"><span class="e">🙄</span>Roll eyes</button>
<button data-fx="lookdown"><span class="e">👇</span>Look down</button>
<button data-fx="grow"><span class="e">🔍</span>Grow eyes</button>
<button data-fx="angry"><span class="e">😠</span>Angry</button>
<button data-fx="sleepy"><span class="e">😴</span>Sleepy</button>
<button data-fx="crosseyed"><span class="e">🤪</span>Cross-eyed</button>
</div>

<h2>Games</h2>
<div class="grid games">
<button data-fx="dice"><span class="e">🎲</span>Roll the dice</button>
<button data-fx="magic"><span class="e">🎱</span>Magic pumpkin</button>
<button data-fx="boo"><span class="e">😱</span>3, 2, 1… BOO!</button>
<button data-fx="surprise"><span class="e">🔀</span>Surprise show</button>
</div>

<h2>Words</h2>
<div class="grid words">
<button data-fx="name"><span class="e">⭐</span>Jack's Pumpkin</button>
<button data-fx="spell"><span class="e">🧙</span>Witches' spell</button>
<button data-fx="trick"><span class="e">🍬</span>Trick or Treat?</button>
<button data-fx="halloween"><span class="e">🦉</span>Happy Halloween!</button>
</div>

<h2>Your own words</h2>
<div class="own">
<input id="words" maxlength="200" placeholder="Type anything…" autocomplete="off">
<button id="say">✨ Scroll it!</button>
</div>
<div id="err"></div>

<button id="show">▶️ Back to the normal show</button>
<!--PHONE-->
</main>
<script>
let seq = 0;    // each press gets a number; only the newest press updates the page
const statusEl = document.getElementById('status');
const err = document.getElementById('err');
const words = document.getElementById('words');
const talk = document.getElementById('talk');

const voice = document.getElementById('voice');

try { talk.checked = localStorage.getItem('talk') !== 'off'; } catch (e) {}
talk.addEventListener('change', () => { try { localStorage.setItem('talk', talk.checked ? 'on' : 'off'); } catch (e) {} });
try {
  const saved = localStorage.getItem('voice');
  if (saved && [...voice.options].some(o => o.value === saved)) voice.value = saved;
} catch (e) {}
voice.addEventListener('change', () => {
  try { localStorage.setItem('voice', voice.value); } catch (e) {}
  fetch('/voicetest?voice=' + encodeURIComponent(voice.value), { method: 'POST' });
});

function talkQuery() {
  return talk.checked ? '?talk=1&voice=' + encodeURIComponent(voice.value) : '';
}

async function go(url, label, body, btn) {
  const mine = ++seq;
  err.textContent = '';
  document.querySelectorAll('button.now').forEach(b => b.classList.remove('now'));
  if (btn) btn.classList.add('now');
  statusEl.textContent = 'Playing: ' + label + ' …';
  statusEl.className = 'playing';
  try {
    const r = await fetch(url + talkQuery(), { method: 'POST', body: body || '' });
    const t = await r.text();
    if (mine !== seq) return;    // a newer press took over
    if (!r.ok) throw new Error(t);
    statusEl.textContent = t;
  } catch (e) {
    if (mine !== seq) return;
    statusEl.textContent = 'Oops, that didn’t work.';
    err.textContent = e.message;
  }
  statusEl.className = '';
  if (btn && !url.endsWith('/surprise')) btn.classList.remove('now');
}

document.querySelectorAll('[data-fx]').forEach(b =>
  b.addEventListener('click', () => go('/play/' + b.dataset.fx, b.textContent.trim(), null, b)));

document.getElementById('say').addEventListener('click', e => {
  const w = words.value.trim();
  if (!w) { err.textContent = 'Type some words first'; words.focus(); return; }
  go('/say', '“' + w + '”', w, e.currentTarget);
});
words.addEventListener('keydown', e => { if (e.key === 'Enter') document.getElementById('say').click(); });
words.addEventListener('input', () => { err.textContent = ''; });
document.getElementById('show').addEventListener('click', e => go('/resume', 'Back to the normal show', null, e.currentTarget));
</script>
</body>
</html>
