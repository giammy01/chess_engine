import pygame
import sys

# Initialize pygame
pygame.init()

# Set window dimensions (small size)
width, height = 600, 500
screen = pygame.display.set_mode((width, height))
pygame.display.set_caption("Black Screen")

# Main loop
running = True
while running:
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False
        elif event.type == pygame.KEYDOWN:
            if event.key == pygame.K_ESCAPE:
                running = False

    # Fill screen with black
    screen.fill((0, 0, 0))

    # Update the display
    pygame.display.flip()

# Quit pygame properly
pygame.quit()
sys.exit()