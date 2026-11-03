"""
Relatorio de uma execucao de bateria (RF09): estimulos, medicoes, vereditos e os dados brutos de cada caso.

- salvar_csv: uma linha por verificacao (separador ';', UTF-8 com BOM, para abrir direto no Excel em portugues);
- salvar_json: tudo, incluindo a area de resultados em hex de cada rotina, para reprocessar depois.
"""

import csv
import json
from dataclasses import asdict, dataclass, field
from datetime import datetime

PASSOU, FALHOU, INCONCLUSIVO = "passou", "falhou", "inconclusivo"


@dataclass
class ResultadoVerificacao:
    descricao: str
    esperado: str
    obtido: str
    passou: bool
    indice_passo: int
    instante_us: int = None       # instante do registro desde o inicio da rotina (None = sem medicao)


@dataclass
class ResultadoCaso:
    nome: str
    identificador: int
    veredito: str
    estado: str = ""              # estado final da rotina (CONCLUIDA, ERRO, ...)
    motivo: str = ""              # por que falhou ou ficou inconclusivo
    duracao_s: float = 0.0
    estimulos: list = field(default_factory=list)
    verificacoes: list = field(default_factory=list)
    resultados_hex: str = ""      # area de resultados lida com READ_RESULT


@dataclass
class Relatorio:
    bateria: str
    dut: str = ""
    arquivo: str = ""
    inicio: str = field(default_factory=lambda: datetime.now().isoformat(timespec="seconds"))
    firmware: str = ""
    porta: str = ""
    casos: list = field(default_factory=list)
    interrompida: bool = False
    motivo: str = ""
    retransmissoes: int = 0
    comandos: int = 0

    def registrar(self, resultado):
        self.casos.append(resultado)

    def contagem(self):
        return {v: sum(c.veredito == v for c in self.casos) for v in (PASSOU, FALHOU, INCONCLUSIVO)}

    @property
    def veredito(self):
        """passou so se todos os casos passaram e a bateria foi ate o fim."""
        if self.interrompida or any(c.veredito == INCONCLUSIVO for c in self.casos):
            return INCONCLUSIVO
        return FALHOU if any(c.veredito == FALHOU for c in self.casos) else PASSOU

    def texto(self):
        linhas = [f"Bateria '{self.bateria}'" + (f" (DUT {self.dut})" if self.dut else "")
                  + (f", firmware da jiga {self.firmware}" if self.firmware else "") + f", {self.inicio}"]
        for caso in self.casos:
            linha = f"  [{caso.veredito.upper():12s}] {caso.identificador}. {caso.nome}"
            if caso.motivo:
                linha += f" -- {caso.motivo}"
            linhas.append(linha)
            for v in caso.verificacoes:
                marca = "ok" if v.passou else "X "
                linhas.append(f"      {marca} {v.descricao}: obtido {v.obtido}")
        n = self.contagem()
        linhas.append(f"Resultado: {self.veredito.upper()} ({n[PASSOU]} passou, {n[FALHOU]} falhou, "
                      f"{n[INCONCLUSIVO]} inconclusivo; {self.comandos} comandos, {self.retransmissoes} retransmissoes)")
        if self.motivo:
            linhas.append(f"Interrompida: {self.motivo}")
        return "\n".join(linhas)

    def salvar_csv(self, arquivo):
        colunas = ["bateria", "dut", "inicio", "firmware", "caso", "nome", "veredito_caso", "estado_rotina", "motivo",
                   "estimulos", "verificacao", "esperado", "obtido", "instante_us", "resultado"]
        with open(arquivo, "w", encoding="utf-8-sig", newline="") as f:
            escritor = csv.writer(f, delimiter=";")
            escritor.writerow(colunas)
            for caso in self.casos:
                base = [self.bateria, self.dut, self.inicio, self.firmware, caso.identificador, caso.nome,
                        caso.veredito, caso.estado, caso.motivo, " | ".join(caso.estimulos)]
                if not caso.verificacoes:
                    escritor.writerow(base + ["", "", "", "", ""])
                for v in caso.verificacoes:
                    escritor.writerow(base + [v.descricao, v.esperado, v.obtido,
                                              "" if v.instante_us is None else v.instante_us,
                                              PASSOU if v.passou else FALHOU])

    def para_dicionario(self):
        dados = asdict(self)
        dados["veredito"] = self.veredito
        dados["contagem"] = self.contagem()
        return dados

    def salvar_json(self, arquivo):
        with open(arquivo, "w", encoding="utf-8") as f:
            json.dump(self.para_dicionario(), f, ensure_ascii=False, indent=2)
