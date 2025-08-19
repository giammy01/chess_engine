# Define the characters to remove (e.g., digits, symbols, or specific letters)
chars_to_remove = "0123456789()',"  # Example: Remove numbers and symbols

# Read the input file
with open("hungarian_words.txt", "r", encoding="utf-8") as file:
    content = file.read()

# Remove unwanted characters
cleaned_content = content
for char in chars_to_remove:
    cleaned_content = cleaned_content.replace(char, "")

# Split into words (handles spaces/newlines)
words = cleaned_content.split()

# Keep only 5-letter words
filtered_words = [word for word in words if len(word) == 5]

# Write the cleaned words to a NEW file (avoid overwriting the original)
with open("cleaned_hungarian_words.txt", "w", encoding="utf-8") as file:
    file.write("\n".join(filtered_words))

print("Specific characters removed and 5-letter words saved to 'cleaned_hungarian_words.txt'")