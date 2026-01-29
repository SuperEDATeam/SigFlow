#include <wx/msgdlg.h>
#include <wx/filename.h> 
#include <wx/sstream.h>
#include <wx/aui/aui.h>

#include "MainFrame.h"
#include "MainMenuBar.h"
#include "CanvasPanel.h"
#include "PropertyPanel.h"
#include "ToolboxPanel.h"   // ��Ĳ����
#include "CanvasModel.h"
#include "my_log.h"
#include "UndoStack.h"
#include "UndoNotifier.h"
#include "ToolBars.h"
#include "HandyToolKit.h"
#include <wx/stc/stc.h>
#include <wx/stdpaths.h>

extern std::vector<CanvasElement> g_elements;

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
EVT_MENU(wxID_EXIT, MainFrame::OnQuit)
EVT_MENU(wxID_HIGHEST + 900, MainFrame::OnToolboxElement)
EVT_MENU(wxID_HIGHEST + 901, MainFrame::OnToolSelected)
wxEND_EVENT_TABLE()


MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "SigFlow")
{
    wxInitAllImageHandlers();
    wxIconBundle icons;
    //icons.AddIcon("res\\icons\\icon_24.png", wxBITMAP_TYPE_PNG);
    //icons.AddIcon("res\\icons\\icon_32.png", wxBITMAP_TYPE_PNG);
    //icons.AddIcon("res\\icons\\icon_64.png", wxBITMAP_TYPE_PNG);
    //icons.AddIcon("res\\icons\\icon_128.png", wxBITMAP_TYPE_PNG);
    icons.AddIcon("res\\icons\\icon_256.png", wxBITMAP_TYPE_PNG);
    SetIcons(icons);

    wxString jsonPath = wxFileName(wxGetCwd(), "canvas_elements.json").GetFullPath();
    MyLog("MainFrame: JSON full path = [%s]\n", jsonPath.ToUTF8().data());
    g_elements = LoadCanvasElements(jsonPath);

    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

    /* �˵� & ״̬�� */
    SetMenuBar(new MainMenuBar(this));
    CreateStatusBar(1);
    int widths[] = { 800, 200, 400, 100 };
    int style[] = { wxSB_NORMAL, wxSB_NORMAL, wxSB_FLAT, wxSB_FLAT };
    GetStatusBar()->SetFieldsCount(4, widths);
    GetStatusBar()->SetStatusStyles(4, style);

    SetTitle("SigFlow [no project]");
    static_cast<MainMenuBar*>(GetMenuBar())->SetCurrentDocInWindowList("Untitled");

    /* �Ѵ��ڽ��� AUI �������������ȣ� */
    m_auiMgr.SetManagedWindow(this);
    m_auiMgr.SetFlags(wxAUI_MGR_DEFAULT | wxAUI_MGR_LIVE_RESIZE);

    /* �������뻭�����ȿհ�ռλ�� */
    m_canvas = new CanvasPanel(this, wxGetDisplaySize().x, wxGetDisplaySize().y);
    m_canvas->SetBackgroundColour(*wxWHITE);
    m_canvas->SetFocus();

    // ������
    m_toolBars = new ToolBars(this);
    AddToolBarsToAuiManager();

    /* ��������� + ���Ա������µ��ţ� */
    wxPanel* sidePanel = new wxPanel(this);  // ���
    wxBoxSizer* sideSizer = new wxBoxSizer(wxVERTICAL);

    ToolboxPanel* toolbox = new ToolboxPanel(sidePanel);  // �������� sidePanel
    sideSizer->Add(toolbox, 1, wxEXPAND);    // �ϣ��������������죩


    sidePanel->SetSizer(sideSizer);

    m_verilogEditor = new SigTextEditor(this);

    m_analysisCenter = new AsyncAnalysisCenter(this);
    this->Bind(EVT_ANALYSIS_COMPLETE, &MainFrame::OnAnalysisComplete, this);
    
    m_refreshTimer = new wxTimer(this, 100000);
    this->Bind(wxEVT_TIMER, &MainFrame::OnRefreshTimer, this, 100000);
    m_refreshTimer->Start(1000);

    m_projectTreePanel = new ProjectTreePanel(this);
    this->Bind(wxEVT_MENU, &MainFrame::OnOpenFileFromTree, this, ID_OPEN_FILE_FROM_TREE);


    /* �������������Ϊһ�� AUI Pane ͣ�� */
    m_auiMgr.AddPane(sidePanel, wxAuiPaneInfo()
        .Name("side")               // ͳһ����
        .Caption("Toolbox & Properties")
        .Left()
        .Layer(1)
        .Position(1)
        .CloseButton(false)
        .BestSize(280, 700)        // �ܸ߶�����������
        .MinSize(200, 400)
        .FloatingSize(280, 700)
        .Gripper(true)
        .PaneBorder(false));

    /* ��������ԭ�� */
    m_auiMgr.AddPane(m_canvas, wxAuiPaneInfo()
        .Name("canvas")
        .CenterPane()
        .CloseButton(false)
        .MinSize(400, 300));

    m_auiMgr.AddPane(m_verilogEditor, wxAuiPaneInfo()
        .Name("text_editor")
        .Caption("Text Editor")
        .Bottom()
        .Layer(1)
        .Position(1)
        .CloseButton(false)
        .BestSize(-1, 250)
        .MinSize(-1, 150)
        .Resizable(true));


    m_auiMgr.AddPane(m_projectTreePanel, wxAuiPaneInfo()
        .Name("project_resource_manager")
        .Caption("Project Resource Manager")
        .Right()
        .Layer(1)
        .Position(1)
        .CloseButton(false)
        .BestSize(280, 700)        // �ܸ߶�����������
        .MinSize(200, 400)
        .FloatingSize(280, 700)
        .Gripper(true)
        .PaneBorder(false)
    );


    /* һ�����ύ */
    m_auiMgr.Update();

    // ���ĳ���֪ͨ
    UndoNotifier::Subscribe([this](const wxString& name, bool canUndo) {
        this->OnUndoStackChanged();
        });

    // 创建属性面板后，显示默认内容
    m_propPanel = new PropertyPanel(sidePanel);
    sideSizer->Add(m_propPanel, 1, wxEXPAND);

    // 显示默认属性
    m_propPanel->ShowElement("Select Tool");
}

MainFrame::~MainFrame()
{
    m_refreshTimer->Stop();
    m_auiMgr.UnInit();   // �����ֶ�����ʼ��
    
}

void MainFrame::OnToolboxElement(wxCommandEvent& evt)
{
    MyLog("MainFrame: received <%s>\n", evt.GetString().ToUTF8().data());

    wxString name = evt.GetString();
    auto it = std::find_if(g_elements.begin(), g_elements.end(),
        [&](const CanvasElement& e) { return e.GetName() == name; });
    if (it == g_elements.end()) return;

    CanvasElement clone = *it;
    clone.SetPos(wxPoint(100, 100));   
    //m_canvas->AddElement(clone);     
    m_canvas->SetCurrentComponent(name);  
}

bool MirrorDirectory(const wxString& source, const wxString& dest) {
    if (!wxDir::Exists(dest)) {
        if (!wxFileName::Mkdir(dest, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
            return false;
        }
    }

    wxDir dir(source);
    if (!dir.IsOpened()) return false;

    wxString filename;
    // 1. 复制所有文件
    bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
    while (cont) {
        wxCopyFile(source + wxFileName::GetPathSeparator() + filename,
            dest + wxFileName::GetPathSeparator() + filename, true);
        cont = dir.GetNext(&filename);
    }

    // 2. 递归处理子目录 (跳过 .git 和 .sigflow 自身，防止无限递归)
    cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_DIRS);
    while (cont) {
        if (filename != ".sigflow" && filename != ".git" && filename != ".cache") {
            MirrorDirectory(source + wxFileName::GetPathSeparator() + filename,
                dest + wxFileName::GetPathSeparator() + filename);
        }
        cont = dir.GetNext(&filename);
    }
    return true;
}


//ֻ�Ǵ�һ���´��ڣ���������д������κθı�
void MainFrame::DoFileOpenProject() {
    wxDirDialog dlg(this, "Open Project Directory", "",
        wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

    if (dlg.ShowModal() == wxID_OK) {
        wxString path = dlg.GetPath();
        m_projectTreePanel->LoadProject(path);
        m_currentProjectPath = path;

        wxString workspacePath = m_currentProjectPath + wxFileName::GetPathSeparator() +
            ".sigflow" + wxFileName::GetPathSeparator() + "workspace";

        //wxLogStatus("Mirroring project to workspace...");

        if (MirrorDirectory(m_currentProjectPath, workspacePath)) {
            //wxLogMessage("Project mirrored to: %s", workspacePath);

            // 2. 更新内部状态
            m_currentProjectPath = m_currentProjectPath;
            m_workspacePath = workspacePath; // 建议在 MainFrame 增加此成员变量
            m_projectName = wxFileName(path).GetFullName();

            // 3. 让左侧树加载原始路径（用户感知），但编译器使用 workspacePath
            m_projectTreePanel->LoadProject(m_currentProjectPath);
            RefreshTitle();
        }
    }
}


void MainFrame::DoFileNew() {
    // ֱ�Ӵ���һ���µĿհ�MainFrame����
    MainFrame* newFrame = new MainFrame();  // ����MainFrame���캯�����ʼ���հ�״̬

    // ��ʾ�´��ڣ�������ʾ��
    newFrame->Centre(wxBOTH);
    newFrame->Show(true);

    // ����ѡ�������Ҫ��¼���д򿪵Ĵ��ڣ������ӵ������б���
    // m_allFrames.push_back(newFrame);  // ��Ҫ��MainFrame��������m_allFrames
}

//�����ļ���ʵ�֣����������ĸ�����
void MainFrame::DoFileSave() {
    //// 1. �����ǰ�ĵ�û��·����δ��������������"����Ϊ"
    //if (m_currentFilePath.IsEmpty()) {
    //    // ����DoFileSaveAs()�����״α��棨��ʵ�ָ÷�����
    //    DoFileSaveAs();
    //    return;
    //}

    //// 2. ���Խ���ǰ�ĵ�����д���ļ�
    //bool saveSuccess = SaveToFile(m_currentFilePath);

    //// 3. ���ݱ���������״̬
    //if (saveSuccess) {
    //    m_isModified = false;  // ����ɹ������Ϊδ�޸�
    //    //UpdateTitle();         // ���´��ڱ��⣨�Ƴ�"*"���޸ı�ǣ�
    //    SetStatusText(wxString::Format("�ѱ���: %s", m_currentFilePath));
    //}
    //else {
    //    wxMessageBox(
    //        wxString::Format("����ʧ��: %s", m_currentFilePath),
    //        "����",
    //        wxOK | wxICON_ERROR,
    //        this
    //    );
    //}
    m_verilogEditor->SaveFile();
}

// ��������������ǰ�ĵ�����д��ָ��·�����޸�ΪXML��ʽ��
bool MainFrame::SaveToFile(const wxString& filePath) {
    // 1. ����XML���ݣ���ʹΪ��Ҳ����д�룩
    wxString xmlContent = GenerateFileContent();

    // 2. ���Դ��ļ�д�루���Դ�ʧ�ܵ������
    wxFile outputFile;
    outputFile.Open(filePath, wxFile::write);  // ���жϴ򿪽����ֱ�ӳ���д��

    // 3. д�����ݣ�����֤д������
    outputFile.Write(xmlContent);
    outputFile.Close();

    // 4. ǿ�Ʒ���true��Ĭ�ϱ���ɹ�
    return true;
}

wxString MainFrame::GenerateFileContent()
{
    // 1. ����XML�ĵ�
    wxXmlDocument doc;

    // 2. ���ڵ� <project>
    wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, "project");
    root->AddAttribute("source", "2.7.1");
    root->AddAttribute("version", "1.0");
    doc.SetRoot(root);

    // 3. ע��
    root->AddChild(new wxXmlNode(wxXML_COMMENT_NODE,
        "This file is intended to be loaded by Logisim "
        "(http://www.cburch.com/logisim/)"));

    // 4. ����Ϣ
    AddLibraryNode(root, "0", "#Wiring");
    AddLibraryNode(root, "1", "#Gates");

    // 5. ��·�ڵ�
    wxXmlNode* circuit = new wxXmlNode(wxXML_ELEMENT_NODE, "circuit");
    circuit->AddAttribute("name", "main");
    root->AddChild(circuit);

    // ����Ԫ����Ϣ���Ƴ���ת�Ƕ���ش��룩
    for (const auto& elem : m_canvas->GetElements()) {
        wxXmlNode* element = new wxXmlNode(wxXML_ELEMENT_NODE, "element");
        element->AddAttribute("name", elem.GetName());  // ����Ԫ������
        element->AddAttribute("x", wxString::Format("%d", elem.GetPos().x));  // ����X����
        element->AddAttribute("y", wxString::Format("%d", elem.GetPos().y));  // ����Y����
        // �Ƴ��������й���rotation�Ĵ���
        // element->AddAttribute("rotation", wxString::Format("%d", elem.GetRotation()));
        circuit->AddChild(element);
    }

    // 7. ����������Ϣ
    for (const auto& wire : m_canvas->GetWires()) {
        // ֱ�ӷ���Wire���pts��Ա������ȡ�㼯��
        const auto& pts = wire.pts;  // �ؼ��޸ģ�ʹ��wire.pts���wire.GetPoints()
        if (pts.size() < 2) continue;

        wxXmlNode* wireNode = new wxXmlNode(wxXML_ELEMENT_NODE, "wire");
        // ������㣨��һ���㣩���յ㣨���һ���㣩
        wireNode->AddAttribute("from", wxString::Format("(%d,%d)", pts[0].pos.x, pts[0].pos.y));
        wireNode->AddAttribute("to", wxString::Format("(%d,%d)", pts.back().pos.x, pts.back().pos.y));

        // �����м�㣨�����ڣ�
        if (pts.size() > 2) {
            wxString midPoints;
            for (size_t i = 1; i < pts.size() - 1; ++i) {
                midPoints += wxString::Format("(%d,%d);", pts[i].pos.x, pts[i].pos.y);
            }
            wireNode->AddAttribute("midpoints", midPoints);
        }
        circuit->AddChild(wireNode);
    }

    // 8. ���XML����
    wxStringOutputStream strStream;
    doc.Save(strStream, wxXML_DOCUMENT_TYPE_NODE);
    return strStream.GetString();
}

void MainFrame::DoFileOpen(const wxString& path)
{
    wxString filePath = path;

    // ���û���ṩ·������ʾ�ļ�ѡ��Ի���
    if (filePath.IsEmpty()) {
        wxFileDialog openDialog(
            this,
            "�򿪵�·�ļ�",
            "",
            "",
            "��·�ļ� (*.circ)|*.circ|�����ļ� (*.*)|*.*",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST
        );

        if (openDialog.ShowModal() != wxID_OK) {
            return;
        }
        filePath = openDialog.GetPath();
    }

    // ���Զ�ȡ�ļ�����
    wxFile file;
    if (!file.Open(filePath, wxFile::read)) {
        wxMessageBox("�޷����ļ�: " + filePath, "����", wxOK | wxICON_ERROR);
        return;
    }

    // ��ȡXML����
    wxString xmlContent;
    file.ReadAll(&xmlContent);
    file.Close();

    // ����XML
    wxXmlDocument doc;
    wxStringInputStream stream(xmlContent);
    if (!doc.Load(stream)) {
        wxMessageBox("�ļ���ʽ����: " + filePath, "����", wxOK | wxICON_ERROR);
        return;
    }

    // ��յ�ǰ����
    m_canvas->ClearAll();

    // �������ڵ�
    wxXmlNode* root = doc.GetRoot();
    if (!root || root->GetName() != "project") {
        wxMessageBox("��Ч�ĵ�·�ļ�", "����", wxOK | wxICON_ERROR);
        return;
    }

    // ���ҵ�·�ڵ�
    wxXmlNode* circuit = root->GetChildren();
    while (circuit) {
        if (circuit->GetName() == "circuit") {
            break;
        }
        circuit = circuit->GetNext();
    }

    if (!circuit) {
        wxMessageBox("�ļ���δ�ҵ���·��Ϣ", "����", wxOK | wxICON_ERROR);
        return;
    }

    // ����Ԫ��������
    wxXmlNode* child = circuit->GetChildren();
    while (child) {
        // ����Ԫ�����Ƴ���ת�Ƕ���ش��룩
        if (child->GetName() == "element") {
            wxString name = child->GetAttribute("name");
            int x = wxAtoi(child->GetAttribute("x", "0"));
            int y = wxAtoi(child->GetAttribute("y", "0"));
            // �Ƴ��������й���rotation�Ķ�ȡ
            // int rotation = wxAtoi(child->GetAttribute("rotation", "0"));

            m_canvas->AddElement(name, wxPoint(x, y));
            // 同时移除设置旋转角度的逻辑（如果有的话）
        }

        // �������ߣ���DoFileOpen�����У�
        else if (child->GetName() == "wire") {
            wxString fromStr = child->GetAttribute("from");
            wxString toStr = child->GetAttribute("to");
            wxString midPointsStr = child->GetAttribute("midpoints", "");

            // ��������� (x,y)
            auto parsePoint = [](const wxString& str) -> wxPoint {
                int x = 0, y = 0;
                if (sscanf(str.ToUTF8().data(), "(%d,%d)", &x, &y) == 2) {
                    return wxPoint(x, y);
                }
                return wxPoint(0, 0);
                };

            // �ؽ�pts����
            std::vector<ControlPoint> pts;
            pts.push_back({ parsePoint(fromStr), CPType::Pin });  // ��㣨Pin���ͣ�

            // �����м��
            if (!midPointsStr.IsEmpty()) {
                wxArrayString midPoints = wxSplit(midPointsStr, ';');
                for (const auto& ptStr : midPoints) {
                    if (ptStr.IsEmpty()) continue;
                    pts.push_back({ parsePoint(ptStr), CPType::Bend });  // �м��Ϊ�۵�
                }
            }

            pts.push_back({ parsePoint(toStr), CPType::Free });  // �յ㣨Free���ͣ�

            // ����Wire�����ӵ�����
            Wire wire;
            wire.pts = pts;  // ֱ�Ӹ�ֵ��Wire��pts��Ա
            wire.GenerateCells();  // ��������㣨������ʾһ���ԣ�
            m_canvas->AddWire(wire);
        }

        child = child->GetNext();
    }

    // ����״̬
    m_currentFilePath = filePath;
    m_isModified = false;
    SetTitle(wxFileName(filePath).GetFullName());
    static_cast<MainMenuBar*>(GetMenuBar())->AddFileToHistory(filePath);
    SetStatusText("�Ѵ�: " + filePath);
}


void MainFrame::OnToolSelected(wxCommandEvent& evt) {
  wxString toolName = evt.GetString();
    std::map<wxString, wxVariant> currentProps;
    
    // 1. 获取当前选中的元件索引
    // 我们需要检查是否有选中的元件，这里假设CanvasPanel有获取选中索引的方法
    // 如果还没有，我们可以先简化处理
    
    // 简化版本：暂时不处理选中状态，直接显示工具属性
    // 你可以在CanvasPanel中添加以下方法来获取选中索引：
    // std::vector<int> GetSelectedElementIndexes() const { return m_selElemIdx; }
    // std::vector<int> GetSelectedWireIndexes() const { return m_selWireIdx; }
    // std::vector<int> GetSelectedTextIndexes() const { return m_selTxtIdx; }
    
    // 临时修复：直接显示属性
    // 3. 显示属性面板
    if (m_propPanel) {
        m_propPanel->ShowElement(toolName, currentProps);
    }
}

// 更新属性面板显示选中的元件
void MainFrame::UpdatePropertyPanel(int elementIndex)
{
    if (!m_propPanel) return;
    
    if (elementIndex < 0 || elementIndex >= (int)m_canvas->GetElements().size()) {
        // 没有选中元件，显示默认
        m_propPanel->ShowElement("Select Tool");
        return;
    }
    
    // 获取选中的元件
    const auto& elements = m_canvas->GetElements();
    if (elementIndex < (int)elements.size()) {
        CanvasElement* elem = const_cast<CanvasElement*>(&elements[elementIndex]);
        m_propPanel->ShowCanvasElement(elem);
    }
}




















// 1. ����ΪBookShelf�淶��.node�ļ�
bool MainFrame::SaveAsNodeFile(const wxString& filePath)
{
    wxFile file;
    // ���Դ��ļ�����ʧ�ܷ���false
    if (!file.Exists(filePath)) {
        if (!file.Create(filePath))
            return false;
    }
    if (!file.Open(filePath, wxFile::write))
        return false;

    wxString content;
    const auto& elements = m_canvas->GetElements();
    int numTotalNodes = elements.size();  // �ܵ�Ԫ�������л���Ԫ����
    int numTerminals = 0;                 // �ն˵�Ԫ������ͳ�ƴ�I/O���ŵ�Ԫ����

    // ��һ����ͳ���ն˵�Ԫ����������/������ŵ�Ԫ����Ϊ�նˣ�
    for (const auto& elem : elements)
    {
        if (!elem.GetInputPins().empty() || !elem.GetOutputPins().empty())
            numTerminals++;
    }

    // �ڶ�����д��.node�ļ�ͷ����NumNodes + NumTerminals��
    content += wxString::Format("NumNodes %d\n", numTotalNodes);
    content += wxString::Format("NumTerminals %d\n", numTerminals);

    // ��������д��ÿ����Ԫ����ϸ��Ϣ����Ԫ�� + ���� + �߶� + �ն˱�ǣ�
    for (const auto& elem : elements)
    {
        // ��ȡ��Ԫ������Ϣ�����ơ�λ�ã����ڼ�����ߣ�
        wxString nodeName = elem.GetName();
        wxRect bounds = elem.GetBounds();  // ͨ��Ԫ���߽�������
        int width = bounds.GetWidth();     // ��Ԫ���ȣ����أ��ɰ����ջ���Ϊ��m���˴��������ص�λ��
        int height = bounds.GetHeight();   // ��Ԫ�߶ȣ�����site�߶ȣ��ĵ�Ĭ��12���˴���ʵ�ʱ߽�ȡ����

        // �ж��Ƿ�Ϊ�ն˵�Ԫ����I/O���ţ�
        bool isTerminal = (!elem.GetInputPins().empty() || !elem.GetOutputPins().empty());

        // ƴ�ӵ�Ԫ�У��ն˵�Ԫ���"terminal"��ǣ���ͨ��Ԫ�����������Ϣ
        if (isTerminal)
        {
            content += wxString::Format("%s %d %d terminal\n",
                nodeName, width, height);
        }
        else
        {
            content += wxString::Format("%s %d %d\n",
                nodeName, width, height);
        }
    }

    // д���ļ����ر�
    file.Write(content);
    file.Close();
    return true;
}

bool MainFrame::SaveAsNetFile(const wxString& filePath)
{
    // ���Դ��������ļ�
    wxFile file;
    if (!file.Exists(filePath))
    {
        if (!file.Create(filePath))
        {
            wxMessageBox("�޷�����.net�ļ���", "����", wxOK | wxICON_ERROR);
            return false;
        }
    }
    if (!file.Open(filePath, wxFile::write))
    {
        wxMessageBox("�޷���.net�ļ�����д�룡", "����", wxOK | wxICON_ERROR);
        return false;
    }

    // �ռ������еĵ��ߺ�Ԫ������
    const auto& wires = m_canvas->GetWires();       // ���軭����GetWires()�������ص����б�
    const auto& elements = m_canvas->GetElements(); // ���軭����GetElements()��������Ԫ���б�
    int numTotalNets = wires.size();
    int numTotalPins = 0;

    // �ṹ�壺�洢���Źؼ���Ϣ������ƥ�䣩
    struct PinInfo {
        wxString cellName;    // ����Ԫ������
        wxString pinType;     // �������ͣ�I/O��
        wxPoint absPos;       // ���ž������꣨��������ϵ��
        wxPoint offset;       // �������Ԫ����ƫ������
    };
    std::vector<PinInfo> allPins;

    // 1. Ԥ��������Ԫ����������Ϣ����������+���ͣ�
    for (const auto& elem : elements)
    {
        wxPoint elemPos = elem.GetPos();          // ��ȡԪ���ڻ����ľ���λ��
        wxString cellName = elem.GetName();

        // ������������
        for (const auto& pin : elem.GetInputPins())
        {
            // �������ž������� = Ԫ��λ�� + �������ƫ��
            wxPoint pinAbsPos(
                elemPos.x + pin.pos.x,
                elemPos.y + pin.pos.y
            );
            allPins.push_back({
                cellName,
                "I",  // �������ű��
                pinAbsPos,
                wxPoint(pin.pos.x, pin.pos.y)  // ���ƫ��
                });
        }

        // �����������
        for (const auto& pin : elem.GetOutputPins())
        {
            wxPoint pinAbsPos(
                elemPos.x + pin.pos.x,
                elemPos.y + pin.pos.y
            );
            allPins.push_back({
                cellName,
                "O",  // ������ű��
                pinAbsPos,
                wxPoint(pin.pos.x, pin.pos.y)
                });
        }

        // ���⴦��"Pin (Output)"��Ԫ����������Ϊ������ţ�
        if (cellName == "Pin (Output)")
        {
            allPins.push_back({
                cellName,
                "O",  // ��ȷΪ�������
                elemPos,  // ����λ�ü�Ϊ����λ��
                wxPoint(0, 0)  // �������ƫ��Ϊ(0,0)
                });
        }
    }

    // 2. �������е��ߣ�����������Ϣ
    wxString content;
    for (int netIdx = 0; netIdx < numTotalNets; ++netIdx)
    {
        const auto& wire = wires[netIdx];
        if (wire.pts.size() < 2)  // ������Ч���ߣ�������Ҫ�����յ㣩
            continue;

        // ���������ߵ������յ���Ϊ��Ч���ţ������м���Ƶ㣩
        std::vector<wxPoint> validPinPositions;
        validPinPositions.push_back(wire.pts[0].pos);                // ���
        validPinPositions.push_back(wire.pts[wire.pts.size() - 1].pos);  // �յ�
        int netDegree = validPinPositions.size();  // �̶�Ϊ2����Ч���ߣ�
        numTotalPins += netDegree;

        // д������ͷ����Ϣ���������+��������
        wxString netName = wxString::Format("n%d", netIdx);
        content += wxString::Format("NetDegree %d %s\n", netDegree, netName);

        // 3. ƥ��ÿ����Ч���ŵ���Ӧ��Ԫ��
        const int COORD_TOLERANCE = 3;  // ����������̣�3��������Ϊƥ�䣩
        for (const auto& pinPos : validPinPositions)
        {
            wxString cellName = "unknown_cell";
            wxString pinType = "I";
            wxPoint offset(0, 0);

            // ����Ԥ����������б�����������ƥ�������
            for (const auto& pinInfo : allPins)
            {
                int dx = abs(pinPos.x - pinInfo.absPos.x);
                int dy = abs(pinPos.y - pinInfo.absPos.y);
                if (dx <= COORD_TOLERANCE && dy <= COORD_TOLERANCE)
                {
                    cellName = pinInfo.cellName;
                    pinType = pinInfo.pinType;
                    offset = pinInfo.offset;
                    break;  // �ҵ�ƥ������ź��˳�ѭ��
                }
            }

            // д��������Ϣ����ʽ��Ԫ���� ����:Xƫ�� Yƫ�ƣ�
            content += wxString::Format("%s %s:%d %d\n",
                cellName, pinType, offset.x, offset.y);
        }
    }

    // 4. д���ļ�ͷ������������������������
    wxString header;
    header += wxString::Format("NumNets %d\n", numTotalNets);
    header += wxString::Format("NumPins %d\n", numTotalPins);
    content = header + content;

    // д���ļ����ر�
    file.Write(content);
    file.Close();
    return true;
}

// 3. �������޸ģ�.node�ļ����津������������ԭ�߼���
void MainFrame::DoFileSaveAsNode()
{
    wxFileDialog saveDialog(this,
        "Save as BookShelf .node File",
        "",
        "circuit.node",  // Ĭ���ļ���
        "BookShelf Node Files (*.node)|*.node",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveDialog.ShowModal() == wxID_CANCEL)
        return;

    wxString filePath = saveDialog.GetPath();
    bool success = SaveAsNodeFile(filePath);
    if (success)
    {
        SetStatusText(wxString::Format("Saved BookShelf .node file: %s", filePath));
    }
    else
    {
        wxMessageBox("Failed to save .node file (check file permissions)", "Error", wxOK | wxICON_ERROR);
    }
}

// 4. �������޸ģ�.net�ļ����津������������ԭ�߼���
void MainFrame::DoFileSaveAsNet()
{
    wxFileDialog saveDialog(this,
        "Save as BookShelf .net File",
        "",
        "circuit.net",  // Ĭ���ļ���
        "BookShelf Net Files (*.net)|*.net",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveDialog.ShowModal() == wxID_CANCEL)
        return;

    wxString filePath = saveDialog.GetPath();
    bool success = SaveAsNetFile(filePath);
    if (success)
    {
        SetStatusText(wxString::Format("Saved BookShelf .net file: %s", filePath));
    }
    else
    {
        wxMessageBox("Failed to save .net file (check file permissions)", "Error", wxOK | wxICON_ERROR);
    }
}






















// ��������������Ԫ����ڵ�
void MainFrame::AddLibraryNode(wxXmlNode* parent, const wxString& name, const wxString& desc) {
    wxXmlNode* lib = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("lib"));
    lib->AddAttribute(wxT("name"), name);
    lib->AddAttribute(wxT("desc"), desc);
    parent->AddChild(lib);
}

// �������������ӵ��߽ڵ�
void MainFrame::AddWireNode(wxXmlNode* parent, const wxString& from, const wxString& to) {
    wxXmlNode* wire = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("wire"));
    wire->AddAttribute(wxT("from"), from);
    wire->AddAttribute(wxT("to"), to);
    parent->AddChild(wire);
}
// ������ʵ��"����Ϊ"�����������״α��棩
void MainFrame::DoFileSaveAs() {
    // �����ļ�ѡ��Ի���
    wxFileDialog saveDialog(
        this,
        "����Ϊ",
        "",
        "Untitled.circ",  // Ĭ���ļ���
        "��·�ļ� (*.circ)|*.circ|�����ļ� (*.*)|*.*",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT  // ��ʾ���������ļ�
    );

    // �û�ȡ���򷵻�
    if (saveDialog.ShowModal() != wxID_OK) {
        return;
    }

    // ��ȡ�û�ѡ���·��
    wxString newPath = saveDialog.GetPath();
    m_currentFilePath = newPath;

    // ִ�б���
    DoFileSave();

    // ����ѡ�����ӵ�����ļ���ʷ
    static_cast<MainMenuBar*>(GetMenuBar())->AddFileToHistory(newPath);
}




void MainFrame::OnAbout(wxCommandEvent&)
{
    wxMessageBox(wxString::Format(wxT("MyLogisim\n%s"), wxVERSION_STRING),
        wxT("About"), wxOK | wxICON_INFORMATION, this);
}

void MainFrame::DoEditUndo()
{
    m_canvas->UndoStackUndo();
    OnUndoStackChanged();   // 刷新菜单
}

void MainFrame::DoEditCut() { wxMessageBox("Edit->Cut"); }
void MainFrame::DoEditCopy() { wxMessageBox("Edit->Copy"); }
void MainFrame::DoEditPaste() { wxMessageBox("Edit->Paste"); }
void MainFrame::DoEditDelete() { 
    m_canvas->GetCanvasEventHandler()->DeleteSelected();
}
void MainFrame::DoEditDuplicate() { wxMessageBox("Edit->Duplicate"); }
void MainFrame::DoEditSelectAll() { wxMessageBox("Edit->SelectAll"); }
void MainFrame::DoEditRaiseSel() { wxMessageBox("Edit->Raise Selection"); }
void MainFrame::DoEditLowerSel() { wxMessageBox("Edit->Lower Selection"); }
void MainFrame::DoEditRaiseTop() { wxMessageBox("Edit->Raise to Top"); }
void MainFrame::DoEditLowerBottom() { wxMessageBox("Edit->Lower to Bottom"); }
void MainFrame::DoEditAddVertex() { wxMessageBox("Edit->Add Vertex"); }
void MainFrame::DoEditRemoveVertex() { wxMessageBox("Edit->Remove Vertex"); }

void MainFrame::DoProjectAddCircuit() { wxMessageBox("Project->Add Circuit"); }
void MainFrame::DoProjectLoadLibrary() { wxMessageBox("Project->Load Library"); }
void MainFrame::DoProjectUnloadLibraries() { wxMessageBox("Project->Unload Libraries"); }
void MainFrame::DoProjectMoveCircuitUp() { wxMessageBox("Project->Move Circuit Up"); }
void MainFrame::DoProjectMoveCircuitDown() { wxMessageBox("Project->Move Circuit Down"); }
void MainFrame::DoProjectSetAsMain() { wxMessageBox("Project->Set As Main Circuit"); }
void MainFrame::DoProjectRemoveCircuit() { wxMessageBox("Project->Remove Circuit"); }
void MainFrame::DoProjectRevertAppearance() { wxMessageBox("Project->Revert Appearance"); }
void MainFrame::DoProjectViewToolbox() { wxMessageBox("Project->View Toolbox"); }
void MainFrame::DoProjectViewSimTree() { wxMessageBox("Project->View Simulation Tree"); }
void MainFrame::DoProjectEditLayout() { wxMessageBox("Project->Edit Circuit Layout"); }
void MainFrame::DoProjectEditAppearance() { wxMessageBox("Project->Edit Circuit Appearance"); }
void MainFrame::DoProjectAnalyzeCircuit() { wxMessageBox("Project->Analyze Circuit"); }
void MainFrame::DoProjectGetStats() { wxMessageBox("Project->Get Circuit Statistics"); }
void MainFrame::DoProjectOptions() { wxMessageBox("Project->Options"); }

void MainFrame::DoSimSetEnabled(bool on)
{
    wxMessageBox(wxString::Format("Simulation %s", on ? "enabled" : "disabled"));
}
void MainFrame::DoSimReset() { wxMessageBox("Simulation reset"); }
void MainFrame::DoSimStep() { wxMessageBox("Simulation step"); }
void MainFrame::DoSimGoOut() { wxMessageBox("Go Out To State"); }
void MainFrame::DoSimGoIn() { wxMessageBox("Go In To State"); }
void MainFrame::DoSimTickOnce() { wxMessageBox("Tick Once"); }
void MainFrame::DoSimTicksEnabled(bool on)
{
    wxMessageBox(wxString::Format("Ticks %s", on ? "enabled" : "disabled"));
}
void MainFrame::DoSimSetTickFreq(int hz)
{
    wxMessageBox(wxString::Format("Tick frequency set to %d Hz", hz));
}
void MainFrame::DoSimLogging() { wxMessageBox("Logging dialog"); }

void MainFrame::DoWindowCombinationalAnalysis()
{
    wxMessageBox("Window->Combinational Analysis");
}
void MainFrame::DoWindowPreferences()
{
    wxMessageBox("Window->Preferences");
}
void MainFrame::DoWindowSwitchToDoc(const wxString& title)
{
    wxMessageBox("Switch to document: " + title);
    // �����߼�����������л�����Ӧ�Ӵ��ڻ���ͼ
}

void MainFrame::DoHelpTutorial()
{
    wxMessageBox("Help->Tutorial");
}
void MainFrame::DoHelpUserGuide()
{
    wxMessageBox("Help->User's Guide");
}
void MainFrame::DoHelpLibraryRef()
{
    wxMessageBox("Help->Library Reference");
}
void MainFrame::DoHelpAbout()
{
    wxMessageBox(wxString::Format("MyLogisim\n%s", wxVERSION_STRING),
        wxT("About"), wxOK | wxICON_INFORMATION, this);
}

void MainFrame::AddToolBarsToAuiManager() {
    if (!m_toolBars) return;

    wxToolBar* toolBar1 = m_toolBars->toolBar1;
    wxToolBar* toolBar2 = m_toolBars->toolBar2;
    //wxToolBar* toolBar3 = m_toolBars->toolBar3;

    // ���ù�������С
    toolBar1->SetSizeHints(-1, 28);
    toolBar2->SetSizeHints(-1, 28);
    //toolBar3->SetSizeHints(-1, 28);

    // ȷ����������ʵ��
    toolBar1->Realize();
    toolBar2->Realize();
    //toolBar3->Realize();

    // ʹ��AUI���������ӹ�����
    m_auiMgr.AddPane(toolBar1, wxAuiPaneInfo()
        .Name("Toolbar1")
        .Caption("Tools")
        .ToolbarPane()
        .Top()
        .Row(0)
        .LeftDockable(false)
        .RightDockable(false)
        .BottomDockable(false)
        .Gripper(false)
        .CloseButton(false)
        .PaneBorder(false)
        .Resizable(false)
        .BestSize(10000, 28));

    m_auiMgr.AddPane(toolBar2, wxAuiPaneInfo()
        .Name("Toolbar2")
        .Caption("Navigation")
        .ToolbarPane()
        .Top()
        .Row(1)  // �ڶ���
        .LeftDockable(false)
        .RightDockable(false)
        .BottomDockable(false)
        .Gripper(false)
        .CloseButton(false)
        .PaneBorder(false)
        .Resizable(false)
        .BestSize(10000, 28));

    //m_auiMgr.AddPane(toolBar3, wxAuiPaneInfo()
    //    .Name("Toolbar3")
    //    .Caption("Actions")
    //    .ToolbarPane()
    //    .Top()
    //    .Row(2)  // ������
    //    .LeftDockable(false)
    //    .RightDockable(false)
    //    .BottomDockable(false)
    //    .Gripper(false)
    //    .CloseButton(false)
    //    .PaneBorder(false)
    //    .Resizable(false)
    //    .BestSize(10000, 28));
    //m_toolBars->ChoosePageOne_toolBar1(-1); // ��ʼ��������״̬
    //m_toolBars->ChoosePageOne_toolBar3(-1); // ��ʼ��������״̬
}

void MainFrame::OnOpenFileFromTree(wxCommandEvent& evt) {
    wxString path = evt.GetString();
    m_verilogEditor->OpenFile(path);   // 你已有的打开文件逻辑
    m_currentFilePath = path;
    RefreshTitle();
}


void MainFrame::OnUndoStackChanged()
{
    wxMenuBar* bar = GetMenuBar();
    if (!bar) return;

    wxMenu* editMenu = bar->GetMenu(1);      // Edit �˵�
    if (!editMenu) return;

    wxMenuItem* undoItem = editMenu->FindItem(wxID_UNDO);
    if (undoItem)
    {
        wxString base = m_canvas->UndoStackGetUndoName(); // "Add AND Gate"
        wxString text = m_canvas->UndoStackCanUndo()
            ? wxString("Undo ") + base + wxString("\tCtrl+Z")
            : wxString("Can't Undo");
        undoItem->SetItemLabel(text);
        undoItem->Enable(m_canvas->UndoStackCanUndo());
    }

}

void MainFrame::OnClose(wxCloseEvent& event) {
    // 1. �����ļ���
    wxString fileName = m_currentFilePath.IsEmpty()
        ? wxString("Untitled")
        : wxFileName(m_currentFilePath).GetFullName();

    // 2. ʹ�� wxMessageDialog ԭ���Ի��򣨽�����������ť��
    // ȥ��ͼ����ز���������Ĭ�ϵ� wxICON_QUESTION����ѡ��
    wxMessageDialog dialog(this,
        wxString::Format("What should happen to your unsaved changes to %s?", fileName),
        "Confirm Close",
        wxYES_NO | wxCANCEL | wxYES_DEFAULT); // ������������ť������

    // 3. ������ť�߼�
    int result = dialog.ShowModal();
    switch (result) {
    case wxID_YES:
        if (m_currentFilePath.IsEmpty()) {
            DoFileSaveAs();
        }
        else {
            DoFileSave();
        }
        event.Skip(); // �����رմ���
        break;
    case wxID_NO:
        event.Skip(); // �����رմ��ڣ������棩
        break;
    case wxID_CANCEL:
        // ��ִ�� Skip()����ֹ���ڹر�
        break;
    }
}

wxString MainFrame::GetWorkspaceCopyPath(const wxString& m_currentFilePath) {
    // 1. 创建文件对象
    wxFileName fileObj(m_currentFilePath);

    // 2. 计算相对于项目根目录的相对路径
    // 执行后，fileObj 将不再存有绝对路径，而是变为 "src/top.v" 这种形式
    if (fileObj.MakeRelativeTo(m_currentProjectPath)) {

        // 3. 将相对路径拼接到工作区根目录下
        // 这里的路径加法会自动处理反斜杠
        wxString targetPath = m_workspacePath + wxFileName::GetPathSeparator() + fileObj.GetFullPath();

        return targetPath;
    }

    return wxEmptyString; // 如果不在项目内，返回空
}

void MainFrame::OnRefreshTimer(wxTimerEvent& event) {
    if (m_verilogEditor->GetModify() && snap_version != m_verilogEditor->GetSnapVersion()) {
        snap_version = m_verilogEditor->GetSnapVersion();
        wxString fullPath = m_verilogEditor->GetCurrentPath();
        wxFileName fn1(fullPath);
        wxString ext = fn1.GetExt().Lower(); // 获取后缀并转换为小写，防止 .V 或 .SV 识别失败

        bool is_verilog = (ext == "v" || ext == "sv" || ext == "vh" || ext == "svh");
        if (!is_verilog) return;

        // 1. 获取最新代码内容
        wxString currentCode = m_verilogEditor->GetText();

        // 2. [优化] 只有内容真正改变才推送，避免仅光标移动触发分析
        // if (currentCode == m_lastCode) return; 

        wxString cachePath = GetWorkspaceCopyPath(fullPath);

        wxFile file(cachePath, wxFile::write);
        // 4. 保存快照：这里建议用 WriteStringToFile 避免干扰 STC 的撤销栈
        if (file.IsOpened()) {
            if (file.Write(currentCode)) {
                file.Close();
                m_analysisCenter->PushTask(m_workspacePath, cachePath);
            }
        }
    }
}

void MainFrame::OnAnalysisComplete(wxThreadEvent& event) {
    AnalysisResult result = event.GetPayload<AnalysisResult>();


    if (result.linted) m_verilogEditor->VisualFeedBack(result.lint);
    //if (result.parsed) m_canvas->
}


// 在 MainFrame 中实现
void MainFrame::RefreshTitle() {
    wxString title = "SigFlow";
    wxFileName fileObj(m_currentFilePath);
    if (fileObj.MakeRelativeTo(m_currentProjectPath)) {
        if (!m_projectName.IsEmpty()) {
            title += " [" + m_projectName  + wxFileName::GetPathSeparator() + fileObj.GetFullPath() + "]";
        }

    }
    else {
        title += " [" + m_projectName + wxFileName::GetPathSeparator() + "]";
    }
    this->SetTitle(title);
}
