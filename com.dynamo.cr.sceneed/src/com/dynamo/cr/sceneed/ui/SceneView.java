package com.dynamo.cr.sceneed.ui;

import javax.inject.Inject;

import org.eclipse.core.resources.IContainer;
import org.eclipse.core.resources.IResource;
import org.eclipse.jface.dialogs.Dialog;
import org.eclipse.jface.viewers.ArrayContentProvider;
import org.eclipse.jface.viewers.ILabelProvider;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.LabelProvider;
import org.eclipse.ui.dialogs.ListDialog;
import org.eclipse.ui.dialogs.ResourceListSelectionDialog;

import com.dynamo.cr.editor.core.EditorUtil;
import com.dynamo.cr.properties.IFormPropertySheetPage;
import com.dynamo.cr.sceneed.core.IRenderView;
import com.dynamo.cr.sceneed.core.ISceneView;
import com.dynamo.cr.sceneed.core.Node;

public class SceneView implements ISceneView {

    @Inject private ISceneOutlinePage outline;
    @Inject private IFormPropertySheetPage propertySheetPage;
    @Inject private IRenderView renderView;
    @Inject private SceneEditor editor;
    @Inject private IContainer contentRoot;
    @Inject private SceneRenderViewProvider sceneRenderViewProvider;

    @Override
    public void setRoot(Node root) {
        this.outline.setInput(root);
        this.sceneRenderViewProvider.setRoot(root);
    }

    @Override
    public void refresh(IStructuredSelection selection, boolean dirty) {
        this.outline.refresh();
        this.propertySheetPage.refresh();
        this.renderView.refresh();
        this.sceneRenderViewProvider.setSelection(selection);
        this.outline.setSelection(selection);
        this.editor.setDirty(dirty);
    }

    @Override
    public String selectFromList(String title, String message, String... lst) {
        ListDialog dialog = new ListDialog(this.editor.getSite().getShell());
        dialog.setTitle(title);
        dialog.setMessage(message);
        dialog.setContentProvider(new ArrayContentProvider());
        dialog.setInput(lst);
        dialog.setLabelProvider(new LabelProvider());

        int ret = dialog.open();
        if (ret == Dialog.OK) {
            Object[] result = dialog.getResult();
            return (String) result[0];
        }
        return null;
    }

    @Override
    public Object selectFromArray(String title, String message, Object[] input, ILabelProvider labelProvider) {
        ListDialog dialog = new ListDialog(this.editor.getSite().getShell());
        dialog.setTitle(title);
        dialog.setMessage(message);
        dialog.setContentProvider(new ArrayContentProvider());
        dialog.setInput(input);
        dialog.setLabelProvider(labelProvider);

        int ret = dialog.open();
        if (ret == Dialog.OK) {
            return dialog.getResult()[0];
        }
        return null;
    }

    @Override
    public String selectFile(String title) {
        ResourceListSelectionDialog dialog = new ResourceListSelectionDialog(this.editor.getSite().getShell(), this.contentRoot, IResource.FILE | IResource.DEPTH_INFINITE);
        dialog.setTitle(title);

        int ret = dialog.open();
        if (ret == Dialog.OK) {
            IResource r = (IResource) dialog.getResult()[0];
            return EditorUtil.makeResourcePath(r);
        }
        return null;
    }

}
