export default defineNuxtConfig({
  compatibilityDate: '2025-01-01',
  ssr: false,
  modules: [
    '@tresjs/nuxt',
    '@pinia/nuxt',
    '@tailwindcss/nuxt',
  ],
  css: ['~/assets/css/globals.css'],
  devtools: { enabled: false },
  experimental: {
    websocket: true,
  },
  nitro: {
    experimental: {
      websocket: true,
    },
  },
})
