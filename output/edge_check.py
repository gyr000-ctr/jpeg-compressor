import subprocess

def col_means(png, w=128, h=64):
    raw = subprocess.run(['convert', png, '-depth', '8', 'rgb:-'],
                         capture_output=True).stdout
    return [sum(raw[(y * w + x) * 3 + 1] for y in range(h)) / h for x in range(w)]

def edge_pos(g):
    for x in range(1, len(g)):
        if g[x] < 128:
            return x + (128 - g[x - 1]) / (g[x] - g[x - 1]) if g[x] != g[x - 1] else float(x)
    return -1

for name, p in [('ours(4:1:1 filtered)', '/tmp/e_f.png'),
                ('libjpeg reference   ', 'output/ref/edge_fancy.png')]:
    print('   ', name, 'edge at x =', round(edge_pos(col_means(p)), 2))
