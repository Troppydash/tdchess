import math
import matplotlib.pyplot as plt

move_overhead = 0
timer = 1*60*1000
inc = 0.1 * 1000
startmove = 11

def time_control(x, original_time_adjust):
    time, inc, moves = x

    ply = moves * 2
    scaled_time = time

    cent_mtg = 5051
    if scaled_time < 1000:
        cent_mtg = int(scaled_time * 5.051)

    time_left = max(
        1,
        time + (inc * (cent_mtg - 100) - move_overhead * (200 + cent_mtg)) // 100
    )

    if original_time_adjust < 0:
        original_time_adjust = 0.3128 * math.log10(time_left) - 0.4354
    logtime_in_sec = math.log10(scaled_time / 1000.0)
    opt_constant = min(
        0.0032116 + 0.000321123 * logtime_in_sec,
        0.00508017
    )

    opt_scale = min(
        0.0121431 + math.pow(ply + 2.94693, 0.461073) * opt_constant,
        0.213035 * time / time_left
    ) * original_time_adjust

    optimum_time = int(opt_scale * time_left)
    # optimum_time = time / 20 + inc / 2
    return max(10, optimum_time), original_time_adjust



moves = []
timers = []
spent = []
total_moves = 200


time_adjust = -1
for move in range(startmove, total_moves):
    moves.append(move)
    timers.append(timer / 1000)

    movetime, time_adjust = time_control([timer, inc, move], time_adjust)
    spent.append(movetime)


    timer -= movetime
    timer -= move_overhead

    if timer < 0:
        print(f"{move} th move out of time")
        break

    timer += inc



fig, ax1 = plt.subplots()
ax2 = ax1.twinx()

ax1.plot(moves, timers, 'r')
ax2.plot(moves, spent, 'b')

fig.tight_layout()
plt.show()