/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        nord: {
          'snow-storm': '#ECF0F1',
          'snow-storm-dark': '#E5E9F0',
          'polar-night': '#2E3440',
          'polar-night-light': '#4C566A',
          'frost': '#5E81AC',
          'frost-light': '#81A1C1',
          'aurora-green': '#A3BE8C',
          'aurora-red': '#BF616A',
          'aurora-orange': '#D08770',
          'aurora-magenta': '#B48EAD',
          'border': '#D8DEE9',
        }
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', 'sans-serif'],
        mono: ['ui-monospace', 'SFMono-Regular', 'Menlo', 'Monaco', 'Consolas', 'monospace'],
      },
      animation: {
        'fade-in-up': 'fadeInUp 0.8s ease-out forwards',
        'pulse': 'pulse 2s cubic-bezier(0.4, 0, 0.6, 1) infinite',
      },
      keyframes: {
        fadeInUp: {
          '0%': { opacity: '0', transform: 'translateY(20px)' },
          '100%': { opacity: '1', transform: 'translateY(0)' },
        }
      },
      typography: (theme) => ({
        DEFAULT: {
          css: {
            color: theme('colors.nord.polar-night-light'),
            h1: {
              color: theme('colors.nord.polar-night'),
              fontFamily: theme('fontFamily.sans'),
            },
            h2: {
              color: theme('colors.nord.frost'),
              fontFamily: theme('fontFamily.sans'),
            },
            h3: {
              color: theme('colors.nord.polar-night'),
              fontFamily: theme('fontFamily.sans'),
            },
            strong: {
              color: theme('colors.nord.polar-night'),
            },
            code: {
              color: theme('colors.nord.aurora-orange'),
              backgroundColor: theme('colors.nord.snow-storm-dark'),
              borderRadius: '0.25rem',
            },
            a: {
              color: theme('colors.nord.frost'),
              '&:hover': {
                color: theme('colors.nord.frost-light'),
              },
            },
            blockquote: {
              borderLeftColor: theme('colors.nord.frost-light'),
              color: theme('colors.nord.polar-night-light'),
            },
          },
        },
      }),
    },
  },
  plugins: [
    require('@tailwindcss/typography'),
  ],
}
