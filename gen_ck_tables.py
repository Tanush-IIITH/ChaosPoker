import itertools

PRIMES = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41]

def rank_bitmask(ranks):
    mask = 0
    for r in ranks:
        mask |= (1 << r)
    return mask

def prime_product(ranks):
    prod = 1
    for r in ranks:
        prod *= PRIMES[r]
    return prod

def is_straight(ranks):
    if len(set(ranks)) != 5:
        return False
    # Ace low straight
    if set(ranks) == {12, 0, 1, 2, 3}:
        return True
    sorted_r = sorted(ranks)
    if sorted_r[4] - sorted_r[0] == 4:
        return True
    return False

# Group exactly 6175 combinations
combos = []
for p in itertools.combinations_with_replacement(range(13), 5):
    counts = {r: p.count(r) for r in set(p)}
    if max(counts.values()) <= 4:
        combos.append(tuple(p))

def hand_strength_tuple(p):
    """Returns a tuple that can be sorted so that best hands are FIRST."""
    counts = {r: p.count(r) for r in set(p)}
    
    four_kind = [r for r, c in counts.items() if c == 4]
    three_kind = [r for r, c in counts.items() if c == 3]
    pairs = sorted([r for r, c in counts.items() if c == 2], reverse=True)
    singles = sorted([r for r, c in counts.items() if c == 1], reverse=True)
    
    if len(four_kind) == 1:
        return (7, four_kind[0], singles[0])
    
    if len(three_kind) == 1 and len(pairs) == 1:
        return (6, three_kind[0], pairs[0])
    
    if len(set(p)) == 5:
        straight = is_straight(p)
        if straight:
            if set(p) == {12, 0, 1, 2, 3}: return (4, 3) # Ace low straight high card is 5 (rank index 3)
            return (4, max(p))
    
    if len(three_kind) == 1:
        return (3, three_kind[0], singles[0], singles[1])
    
    if len(pairs) == 2:
        return (2, pairs[0], pairs[1], singles[0])
    
    if len(pairs) == 1:
        return (1, pairs[0], singles[0], singles[1], singles[2])
    
    # Otherwise High Card
    # Note: straights already handled (type 4), but what if it's flush vs non-flush?
    # Our strength tuple here ignores flushes. We process the categories externally.
    
    # We will just evaluate non-straight 5-unique
    return (0, singles[0], singles[1], singles[2], singles[3], singles[4])

# Straight Flushes
straight_flushes = []
four_of_a_k = []
full_house = []
flushes = []
straights = []
three_of_a_k = []
two_pair = []
one_pair = []
high_cards = []

for c in combos:
    counts = {r: c.count(r) for r in set(c)}
    if max(counts.values()) == 4:
        four_of_a_k.append(c)
    elif max(counts.values()) == 3 and 2 in counts.values():
        full_house.append(c)
    elif max(counts.values()) == 3:
        three_of_a_k.append(c)
    elif list(counts.values()).count(2) == 2:
        two_pair.append(c)
    elif list(counts.values()).count(2) == 1:
        one_pair.append(c)
    else:
        # unique 5
        if is_straight(c):
            # both sf and straight will use the same combos
            straight_flushes.append(c)
            straights.append(c)
        else:
            flushes.append(c)
            high_cards.append(c)

# Sort each category from BEST to WORST
straight_flushes.sort(key=hand_strength_tuple, reverse=True)
four_of_a_k.sort(key=hand_strength_tuple, reverse=True)
full_house.sort(key=hand_strength_tuple, reverse=True)
flushes.sort(key=hand_strength_tuple, reverse=True)
straights.sort(key=hand_strength_tuple, reverse=True)
three_of_a_k.sort(key=hand_strength_tuple, reverse=True)
two_pair.sort(key=hand_strength_tuple, reverse=True)
one_pair.sort(key=hand_strength_tuple, reverse=True)
high_cards.sort(key=hand_strength_tuple, reverse=True)

rank_counter = 1

def assign_ranks(combo_list):
    global rank_counter
    res = {}
    for c in combo_list:
        res[c] = rank_counter
        rank_counter += 1
    return res

sf_dict = assign_ranks(straight_flushes)
quads_dict = assign_ranks(four_of_a_k)
fh_dict = assign_ranks(full_house)
flush_dict = assign_ranks(flushes)
straight_dict = assign_ranks(straights)
trips_dict = assign_ranks(three_of_a_k)
twopair_dict = assign_ranks(two_pair)
onepair_dict = assign_ranks(one_pair)
highcard_dict = assign_ranks(high_cards)

print("Final rank assigned:", rank_counter - 1)  # should be 7462

flushes_table = [0] * 8192
unique5_table = [0] * 8192
prime_table = []

for c, r in sf_dict.items():
    flushes_table[rank_bitmask(c)] = r
for c, r in flush_dict.items():
    flushes_table[rank_bitmask(c)] = r

for c, r in straight_dict.items():
    unique5_table[rank_bitmask(c)] = r
for c, r in highcard_dict.items():
    unique5_table[rank_bitmask(c)] = r

for c, r in quads_dict.items():
    prime_table.append((prime_product(c), r))
for c, r in fh_dict.items():
    prime_table.append((prime_product(c), r))
for c, r in trips_dict.items():
    prime_table.append((prime_product(c), r))
for c, r in twopair_dict.items():
    prime_table.append((prime_product(c), r))
for c, r in onepair_dict.items():
    prime_table.append((prime_product(c), r))

# Sort prime table by prime product for binary search
prime_table.sort(key=lambda x: x[0])

# C++ output
with open("ck_tables.h", "w") as f:
    f.write("#pragma once\n\n")
    f.write("#include <cstdint>\n\n")
    
    f.write("static const uint16_t flushes[8192] = {\n  ")
    for i in range(8192):
        f.write(f"{flushes_table[i]}")
        if i < 8191:
            f.write(", ")
        if i % 16 == 15:
            f.write("\n  ")
    f.write("\n};\n\n")
    
    f.write("static const uint16_t unique5[8192] = {\n  ")
    for i in range(8192):
        f.write(f"{unique5_table[i]}")
        if i < 8191:
            f.write(", ")
        if i % 16 == 15:
            f.write("\n  ")
    f.write("\n};\n\n")
    
    f.write(f"struct PrimeRecord {{\n    uint32_t prime_product;\n    uint16_t rank;\n}};\n\n")
    f.write(f"static const PrimeRecord prime_products[{len(prime_table)}] = {{\n")
    
    for pp, rank in prime_table:
        f.write(f"  {{{pp}, {rank}}},\n")
    
    f.write("};\n")
