// Note: `npm run lint` is intentionally not part of `npm run compile`.
// ESLint 9.x uses self-referential package imports (require("eslint/lib/..."))
// that fail under Node.js 22's stricter exports enforcement. This is a known
// ESLint upstream bug. TypeScript type-checking (`npm run check-types`) serves
// as the build-blocking quality gate; lint can be run separately once a fixed
// ESLint version is released.
import typescriptEslint from "typescript-eslint";

export default [
  {
    files: ["**/*.ts"],
  },
  {
    plugins: {
      "@typescript-eslint": typescriptEslint.plugin,
    },

    languageOptions: {
      parser: typescriptEslint.parser,
      ecmaVersion: 2022,
      sourceType: "module",
    },

    rules: {
      "@typescript-eslint/naming-convention": [
        "warn",
        {
          selector: "import",
          format: ["camelCase", "PascalCase"],
        },
      ],

      curly: "warn",
      eqeqeq: "warn",
      "no-throw-literal": "warn",
      semi: "warn",
    },
  },
];
