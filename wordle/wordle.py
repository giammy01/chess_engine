from collections import Counter

with open("cleaned_hungarian_words.txt", "r", encoding="utf-8") as f:
    list = [line.strip() for line in f]  # strip removes \n

# Define banned and required letters
banned_letters = set("")  # exclude any word containing these letters
required_letters = set("")  # word must include all these letters
# 1: 'i'
correct_positions = {}  # letters that must be in specific positions
# 'i': [1]
wrong_positions = {}


# Step 1: Count frequency of letters in all words
letter_counts = Counter("".join(list).lower())


# Step 2: Word scoring (high frequency letters = higher score)
def word_score(word):
    return sum(letter_counts[ch] for ch in set(word))


# Step 3: Filter function
def valid_word(word):
    # Must match correct positions
    for pos, letter in correct_positions.items():
        if pos >= len(word) or word[pos] != letter:
            return False

    # Must not contain banned letters
    if set(word) & banned_letters:
        return False

    # Must contain required letters
    if not required_letters.issubset(set(word)):
        return False

    # Check for letters in wrong positions
    for letter, positions in wrong_positions.items():
        if letter not in word:
            return False  # This should be caught by required_letters check, but just in case
        for pos in positions:
            if pos < len(word) and word[pos] == letter:
                return False

    return True


# Step 4: Apply filters and rank
filtered_words = [w for w in list if valid_word(w)]
ranked_words = sorted(filtered_words, key=word_score, reverse=True)

print("Ranked words:", ranked_words)