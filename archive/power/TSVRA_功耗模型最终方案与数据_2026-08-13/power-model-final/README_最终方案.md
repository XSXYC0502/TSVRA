# TSVRA 功耗模型最终方案（45 nm）

## 结论

本包采用一个固定且可复现的最终方案：在 **45 nm、1.0 V、1 GHz** 条件下，以 TSVRA 的网络规模、流量、故障/局簇、重路由和 TSV 几何参数为输入，预测整个网络的 `network_total_power_mW`。

数据不是随意随机标签，而是用互连开关能量公式、文献 TSV 电容参数和 DSENT 路由器功耗数据联合标定后生成的架构级样本。它适合 SRTP 阶段的模型训练、故障策略比较和功耗趋势分析；不应被表述为芯片流片实测数据或 sign-off 功耗。

## 交付内容

- `TSVRA_45nm功耗模型最终训练数据.xlsx`：方案、数据字典、完整训练数据和校准来源。
- `数据来源说明.md`：文献出处、采用参数、生成公式和适用边界。
- `data/tsvra_power_training_45nm.csv`：12,000 条可直接训练的样本。
- `data/dataset_manifest.json`：数据版本、划分和范围。
- `models/network_power_hist_gradient_boosting.joblib`：已训练模型。
- `train_power_model.py`：从 CSV 重新训练并生成评估结果。
- `predict_power.py`：对新 TSVRA 参数表批量预测。
- `results/metrics.json`：验证集与测试集指标。
- `results/test_predictions.csv`：测试集逐条预测。
- `results/feature_importance.csv`：置换特征重要性。

## 数据与任务定义

- 样本数：12,000
- 划分：训练 8,393；验证 1,792；测试 1,815
- 输入：18 个数值特征和 2 个类别特征
- 主标签：`network_total_power_mW`
- 固定技术域：45 nm、1.0 V、1 GHz
- 随机种子：20260813
- 标签范围：17.835–20,647.149 mW，均值 1,696.321 mW

建议先只训练主标签，不把同表内的中间功耗分量作为输入，避免标签泄漏。

## 已验证基线

最终模型为 `HistGradientBoostingRegressor`：

- 验证集：R² 0.99685，MAE 60.76 mW，MAPE 5.77%
- 测试集：R² 0.99615，MAE 58.37 mW，MAPE 6.22%

这些指标衡量的是模型复现当前物理—文献标定生成器的能力，并不等同于相对真实芯片测量的误差。

## 使用方法

在本目录执行：

```powershell
python -m pip install -r requirements.txt
python train_power_model.py
```

对包含必需特征列的新 CSV 预测：

```powershell
python predict_power.py new_samples.csv -o power_predictions.csv
```

最稳妥的新样本模板是从训练 CSV 中复制表头和一行，再替换输入参数。`predict_power.py` 会忽略额外列，但缺少必需列时会明确报错。

## 建模口径

路由器功耗以 DSENT 45 nm、64 bit、1 GHz 案例进行标定；水平与垂直互连按 `0.5 × C × V²` 的开关能量关系计算，TSV 电容采用 Kim 等人的 TSV 几何案例。故障率、局簇强度和重路由率通过有效注入、跳数与冗余垂直传输影响总功耗。

来源：

- DSENT: A Tool Connecting Emerging Photonics with Electronics for Opto-Electronic Networks-on-Chip — https://projects.csail.mit.edu/wiki/pub/LSPgroup/PublicationList/dsent_nocs12.pdf
- A Fast and Accurate Analytical Modeling of Through-Silicon-Via Capacitive Coupling — https://www.gtcad.gatech.edu/www/papers/p25-kim.pdf

## 使用边界与后续升级

不要把 45 nm 模型直接外推到其他工艺、电压或频率。后续拿到 Vivado/板级/芯片实测数据时，保留现有 20 个输入字段，新增 `measured_power_mW`，用实测标签重新训练或对当前预测做残差校准，即可平滑升级为实验数据模型。
