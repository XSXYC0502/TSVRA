import tailwindcss from '@tailwindcss/vite'

export default defineNuxtConfig({
  compatibilityDate: '2025-01-01',
  ssr: false,
  modules: [
    '@pinia/nuxt',
  ],
  css: ['~/assets/css/globals.css'],
  devtools: { enabled: false },
  vite: {
    plugins: [
      tailwindcss(),
    ],
  },
  experimental: {
    websocket: true,
  },
  nitro: {
    experimental: {
      websocket: true,
    },
  },
})
