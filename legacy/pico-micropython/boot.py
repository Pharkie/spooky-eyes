# boot.py
import machine
import time

# Custom logging function
def log_message(message, mode='a'):
    with open('boot_log.txt', mode) as log_file:
        current_time = time.localtime()
        formatted_time = "{:04}-{:02}-{:02} {:02}:{:02}:{:02}".format(
            current_time[0], current_time[1], current_time[2],
            current_time[3], current_time[4], current_time[5]
        )
        log_file.write(f"{formatted_time} - {message}\n")

# Wipe the log file blank on each boot
log_message("Booting device", mode='w')