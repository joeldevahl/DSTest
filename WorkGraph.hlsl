struct EntryRecord
{
    uint gridSize : SV_DispatchGrid;
    uint recordIndex;
};


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(16, 1, 1)]
[NumThreads(2, 1, 1)]
void firstNode(DispatchNodeInputRecord<EntryRecord> inputData)
{
}