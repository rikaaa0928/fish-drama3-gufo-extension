{
  description = "Fish Audio Drama-3 TTS source extension for Gufo";
  inputs.gufo.url = "github:gufo-org/gufo/6a0274393b0cb4971c7c02eb2370cf6bfba7cc48";
  outputs = { self, gufo }: {
    packages.x86_64-linux.default = gufo.packages.x86_64-linux.default.override {
      extensions = [ self.outPath ];
    };
  };
}
